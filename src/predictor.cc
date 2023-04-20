#include "predictor.h"

#include "predict_db.h"
#include <rime/candidate.h>
#include <rime/context.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/menu.h>
#include <rime/segmentation.h>
#include <rime/service.h>
#include <rime/schema.h>
#include <rime/translation.h>
#include <rime/config.h>
#include <utf8.h>


static const char* kPlaceholder = " ";
static const char kRimeAlphabel[]= "zyxwvutsrqponmlkjihgfedcba";

namespace rime {

inline size_t utf8_lenght(const string& str){
  return  utf8::unchecked::distance(str.c_str(), str.c_str() + str.length());
}
string utf8_substr_(const std::string& str, int32_t start, int32_t leng)
{
    if (leng==0) { return ""; }
    int32_t c, i, ix, q;
    auto min = std::string::npos;
    auto max = std::string::npos;
    for (q=0, i=0, ix=str.length(); i < ix; i++, q++)
    {
        if (q==start){ min=i; }
        if (q<=start+leng || leng==std::string::npos){ max=i; }

        c = (unsigned char) str[i];
        if      (
                 //c>=0   &&
                 c<=127) i+=0;
        else if ((c & 0xE0) == 0xC0) i+=1;
        else if ((c & 0xF0) == 0xE0) i+=2;
        else if ((c & 0xF8) == 0xF0) i+=3;
        //else if (($c & 0xFC) == 0xF8) i+=4; // 111110bb //byte 5, unnecessary in 4 byte UTF-8
        //else if (($c & 0xFE) == 0xFC) i+=5; // 1111110b //byte 6, unnecessary in 4 byte UTF-8
        else return "";//invalid utf8
    }
    if (q<=start+leng || leng==std::string::npos){ max=i; }
    if (min==std::string::npos || max==std::string::npos) { return ""; }
    return str.substr(min,max);
}

string utf8_substr(const string& str,int start, int len=-1){

  if (len == 0) {
    return "";
  }
  size_t u8len= utf8_lenght(str);
  if (start <0){
    start += u8len;
    if (start <0)
      start = 0;
  }
  if (len<0)
    len = u8len -start;
  return utf8_substr_(str, start, len);
}

string get_history_text(Context* ctx, int len){
  auto cm=ctx->commit_history();
  string text;
  for (auto rit = cm.rbegin(); rit != cm.rend(); rit++) {
    if (rit->type == "punct" ||
        rit->type == "raw" ||
        rit->type == "thru") {
      continue;
    } else {
      text = utf8_substr( rit->text + text,-(len));
      if ( utf8_lenght(text) >=len) {
        break;
      }
    }
  }
  return text;
}

static inline bool belongs_to(char ch, const string& charset) {
  return charset.find(ch) != std::string::npos;
}

Predictor::Predictor(const Ticket& ticket, PredictDb* db)
    : Processor(ticket), db_(db), alphabel_(kRimeAlphabel),history_lenght_(3) {
  if (name_space_ == "processor"){
    name_space_="predictor";
  }
  if (Config* config = engine_->schema()->config()) {
    config->GetString("speller/alphabel", &alphabel_);
    config->SetBool(name_space_ + "/enable_completion", true);
    config->SetString(name_space_ + "/tag" , "prediction");
    config->GetInt(name_space_ + "/history_lenght", &history_lenght_);

  }
  if (auto c = Translator::Require("table_translator")){
    translator_.reset( c->Create(Ticket(engine_,name_space_, "table_translator")));
  }


  // update prediction on context change.
  auto* context = engine_->context();
  select_connection_ =
      context->select_notifier().connect(
          [this](Context* ctx) {
            OnSelect(ctx);
          });
  context_update_connection_ =
      context->update_notifier().connect(
          [this](Context* ctx) {
            OnContextUpdate(ctx);
          });
}

Predictor::~Predictor() {
  select_connection_.disconnect();
  context_update_connection_.disconnect();
}

ProcessResult Predictor::ProcessKeyEvent(const KeyEvent& key_event) {
  auto ch = key_event.keycode();
  auto* ctx = engine_->context();
  if ( ch == XK_BackSpace) {
    last_action_ = kDelete;
    if (!ctx->composition().empty() &&
        ctx->composition().back().HasTag("prediction")) {
      ctx->PopInput(ctx->composition().back().length);
      return kAccepted;
    }
  } else if ( ch < 0x7f && belongs_to(ch, alphabel_) ){
    last_action_ = kUnspecified;
    if (!ctx->composition().empty() &&
        ctx->composition().back().HasTag("prediction")) {
      LOG(INFO) <<" keyevent ascii code and belone to";
      ctx->PopInput(ctx->composition().back().length);
      ctx->RefreshNonConfirmedComposition();
    }
  } else {
    last_action_ = kUnspecified;
  }
  return kNoop;
}

void Predictor::OnSelect(Context* ctx) {
  if (ctx->get_option('predict'))
    last_action_ = kSelect;
}

void Predictor::OnContextUpdate(Context* ctx) {
  if (!db_ || !ctx || !ctx->composition().empty())
    return;
  if (last_action_ == kDelete) {
    return;
  }
  string t = get_history_text(ctx, history_lenght_);
  Predict(ctx, t);
}

class PredictTranslation : public FifoTranslation {
  public:
    PredictTranslation(PredictDb* db, const string& prefix, const Segment& segment) {
      if (prefix.size()==0)
        return;
      if (auto candidates = db->Lookup(prefix)) {
        for (auto* it =candidates->begin(); it != candidates->end(); ++it) {
          auto cand = New<SimpleCandidate>(
              "prediction", segment.start, segment.end, db->GetEntryText(*it) );

          cand->set_comment("--" + std::to_string(cand->quality() ) );
          Append(cand);
        }
      }
    }
};
typedef an<Candidate>(*shodow_func)(an<Candidate> cand, const string& prefix);
class ShadowTranslation : public PrefetchTranslation {
  public:
    ShadowTranslation(an<Translation> translation, const string& prefix, shodow_func func)
      : PrefetchTranslation(translation), prefix_(prefix), func_(func) {};
  protected:
    shodow_func func_;
    string prefix_;
    virtual bool Replenish(){
      auto next = translation_->Peek();
      translation_->Next();
      if (next) {
        cache_.push_back( (*func_)(next, prefix_ ));
      }
      return !cache_.empty();
    }
};

an<Candidate> predict_shadow(an<Candidate> cand, const string& prefix) {
  size_t f_idx = cand->text().find(prefix);
  if (f_idx == 0 && cand->text().length() > prefix.length() ){
    auto text = cand->text().substr(prefix.length());
    cand->set_quality( 1.0 + cand->quality());
    return New<ShadowCandidate>(cand,cand->type(), text,
        cand->comment()+" essay " + std::to_string(cand->quality()));
  }
  return {};
}

void Predictor::Predict(Context* ctx, const string& context_query) {
  // init segment of prediction;

  ctx->set_input(kPlaceholder);
  int end = ctx->input().length();
  Segment segment(0,end);
  segment.status = Segment::kGuess;
  segment.tags.insert("prediction");

  ctx->composition().AddSegment(segment);
  ctx->composition().back().tags.erase("raw");
  auto menu = New<Menu>();
  ctx->composition().back().menu = menu;
  string active_text(context_query);
  bool state= false;
  auto predict_translation = New<UnionTranslation>();
  auto essay_translation= New<UnionTranslation>();

  for (auto len=utf8_lenght( active_text ); len >=0; len--) {
    if (active_text.size()<=0) {
      *predict_translation += New<PredictTranslation>(db_, "$", segment);
      break;
    }

    *essay_translation += New<ShadowTranslation>(
        translator_->Query(active_text,segment), active_text, predict_shadow);
    active_text = utf8_substr(active_text,1);
  }

  if (!predict_translation->exhausted() && essay_translation->exhausted()) {
      return ;
  }
  if (!predict_translation->exhausted()) {
    menu->AddTranslation(predict_translation);
  }
  if (!essay_translation->exhausted()) {
    menu->AddTranslation(essay_translation);
  }
  LOG(INFO) << "predict prefix string :" << context_query << ":" << context_query.length() ;
  // add segment
}

PredictorComponent::PredictorComponent() {}


PredictorComponent::~PredictorComponent() {}

Predictor* PredictorComponent::Create(const Ticket& ticket) {
  if (!db_) {
    the<ResourceResolver> res(
        Service::instance().CreateResourceResolver({"predict_db", "", ""}));
    auto db = std::make_unique<PredictDb>(
        res->ResolvePath("predict.db").string());
    if (db && db->Load()) {
      db_ = std::move(db);
    }
  }
  return new Predictor(ticket, db_.get());
}

}  // namespace rime
