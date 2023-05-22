#include "utf8_utils.h"
#include "predict_db.h"
#include "predict_translator.h"
#include <assert.h>

#include <rime/context.h>
#include <rime/service.h>
#include <rime/engine.h>
#include <rime/schema.h>
#include <rime/menu.h>
#include <rime/gear/table_translator.h>
#include <rime/segmentation.h>
#include <rime/gear/translator_commons.h>
//#include <rime/config.h>
//
//translation 
//query 
//  switch input from  history input property 
//  predictdb  db active_input 
//
//
//  
//  switch input from  history input property 
//  
//  with_candidate (has_menu)
//  with_match_text (cand.text == active_input)
//  table  active_input and wrap shadow
//
namespace rime {

static const char* kPlaceholder = " ";
static const int kHistoryLength = 3;

class PredictDbTranslation : public FifoTranslation {
  public:
    PredictDbTranslation(PredictDb *db, const string& prefix, const Segment& segment, double quality=1.0);
    ~PredictDbTranslation() {};
};

PredictDbTranslation::PredictDbTranslation(PredictDb *db, const string& prefix, const Segment& segment, double quality) {
  if (auto cands = db->Lookup(prefix)) {
    for (auto* it=cands->begin() ; it != cands->end() ; it++){
      auto cand =New<SimpleCandidate>("prediction", segment.start, segment.end, db->GetEntryText(*it), "pre", prefix);
      cand->set_quality(quality);
      Append(cand);
    }
  }
}



typedef std::function<an<Translation> (const string&)> predict_func;
class PredictTranslation : public Translation{
  public:
    PredictTranslation( const string& prefix, predict_func func);
    bool Next();
    an<Candidate> Peek();
  protected:
    bool Replenish();
    string prefix_;
    predict_func func_;
    an<Translation> translation_;
};

PredictTranslation::PredictTranslation(const string& prefix, predict_func func)
: prefix_(prefix), func_(func){
  set_exhausted( Replenish() );
}

bool PredictTranslation::Next() {
  if(exhausted()) {
    return false;
  }
  if (translation_->Next()){
    return true;
  }
  return Replenish();
}
an<Candidate> PredictTranslation::Peek() {
  if (exhausted()) {
    return nullptr;
  }
  if (auto cand = translation_->Peek()) {
    return cand;
  }
  else {
    if (Replenish())
      return Peek();
    else
      return {};
  }
}

bool PredictTranslation::Replenish(){
  LOG(INFO) << "PredictTranslation prefix_: " << prefix_;
  if (prefix_.empty()){
    set_exhausted(true);
    return false;
  };
  translation_ = func_(prefix_);
  auto n= prefix_.c_str();
  utf8::unchecked::next(n);
  string res = prefix_.substr(n - prefix_.c_str());
  if (res.length() == 0) {
    prefix_ = (prefix_== "$") ? "" : "$";
  }
  else {
    prefix_ = res;
  }
  LOG(INFO) << "PredictTranslation prefix_: " << prefix_;

  if (!translation_ || translation_->exhausted()){
    return Replenish();
  }
  return true;
}


class PredictTranslation1 : public UnionTranslation{
  public:
    PredictTranslation1( const string& prefix, predict_func func)
      : prefix_(prefix), func_(func){ Replenish() ; };
    //bool Next();
    //an<Candidate> Peek();
  protected:
    bool Replenish();
    string prefix_;
    predict_func func_;
};

bool PredictTranslation1::Replenish(){
  do {
    auto t = func_(prefix_); // <<---------- Bug 找不到時會異常
      
    if (t && !t->exhausted()){
      *this += t;
    }
    string res = utf8_substr(prefix_, 1);
    prefix_ = (res.length() == 0 && prefix_ != "$") ? "$" : res;
  }while( !prefix_.empty()  );
  return !exhausted();
}


// ShadowTranslation 
typedef std::function<an<Candidate>(an<Candidate> cand, const string&, const string&)> shadow_func;
class ShadowTranslation : public CacheTranslation{
  public:
    ShadowTranslation(an<Translation> translation,
        shadow_func func, const string& prefix, const string& last_matcm = "" );
    an<Candidate> Peek();
  protected:
    string prefix_;
    string last_match_;
    shadow_func func_;
    //an<Translation> translation_;
};

// ShadowTranslation  table candidate to predict candidate
an<Candidate> predict_shadow(an<Candidate> cand, const string& prefix, const string& last_match) {
  size_t f_idx = cand->text().find(prefix);
  if (f_idx == 0 && cand->text().length() >= prefix.length() ){
    auto text = cand->text().substr(prefix.length());
    double fix_quality = (cand->type()=="completion") ? 1.0 : 0;
    cand->set_quality( fix_quality + cand->quality());
    return New<ShadowCandidate>(
        cand,cand->type(), text,"tab",false);
  }
  return {};
}
an<Candidate> table_shadow(an<Candidate> cand, const string& prefix, const string& last_match) {
  size_t f_idx = cand->text().find(prefix);
  if (f_idx == 0 && cand->text().length() > prefix.length() ){

    auto text = cand->text().substr(cand->preedit().length());
    double fix_quality = (cand->type()=="completion") ? 1.0 : 0;
    cand->set_quality( fix_quality + cand->quality());
    auto scand= New<ShadowCandidate>( cand,cand->type(), text,"tab",false);
    return  scand;
  }
  return {};
}

an<Candidate> predict_shadow2(an<Candidate> cand, const string& last_match) {
  return {};
}

ShadowTranslation::ShadowTranslation(an<Translation> translation,
        shadow_func func, const string& prefix, const string& last_match) : 
  CacheTranslation(translation), prefix_(prefix), last_match_(last_match), func_(func) {
  }


an<Candidate> ShadowTranslation::Peek(){
  if (exhausted())
    return nullptr;
  if (cache_)
    return cache_;

  while( auto cand = translation_->Peek()) {
    if (auto cand = translation_->Peek()){
      if (auto scand= func_(cand, prefix_, last_match_)){
        cache_ = scand;
        return cache_;
      }
    }
    if(! Next()) {
      break;
    }
  }
  set_exhausted(true);
  return nullptr;
}

void PredictTranslator::set_prefix_from(const string& str){
  if (str == "history")
    prefix_from_ =kHistory;
  else if (str == "property")
    prefix_from_ = kProperty;
  else if (str == "input")
    prefix_from_ = kInput;
  else 
    prefix_from_ = kHistory;
}


static string get_history_text(Context* ctx, size_t len){
  string text;
  if (ctx->IsComposing() ){
    auto seg = ctx->composition().back();
    if (seg.status < Segment::kSelected ) {
      text = ctx->composition().GetTextBefore(seg.start);
    }
    else {
      text = ctx->GetCommitText();
    }
  }
  LOG(INFO)<<"-----test:("<< text <<")";
  auto cm=ctx->commit_history();
  LOG(INFO) << "===get_history_text===" << text << "=cm:" << cm.empty();
  if (cm.empty() && text.empty() ){
    return "$";
  }
  string res = utf8_substr( text , -(len));
  if (utf8_length(res) >= len )  {
    return res;
  }
  for (auto rit = cm.rbegin(); rit != cm.rend(); rit++) {
    if (! (rit->type == "punct" ||
        rit->type == "raw" ||
        rit->type == "thru" ||
        rit->text.empty() ))
    {
      text.insert(0, rit->text);
      string res = utf8_substr( text , -(len));
      if (utf8_lenght(res) >= len )  {
        return res;
      }
    }
  }
  return text;
}

an<Translation> PredictTranslator::Query(const string& input, const Segment& segment ) {
  if (!segment.HasTag(tag_) ){
    return nullptr;
  }

  // init active_input
  string active_input;
  string from;
  Context* ctx= engine_->context();
  if (prefix_from_== kHistory){
     from = "history";
     LOG(INFO) << ctx->commit_history().repr();
     active_input= get_history_text(ctx, history_length_);
  } else if (prefix_from_ == kProperty) {
    from = "property";
    active_input = utf8_substr( ctx->get_property("prediction") ,-(history_length_));
  } else if (prefix_from_ == kInput) {
    from = "input";
    active_input = input;
  } else{
    return {};
  }
  LOG(INFO) << "active_input from " << from << ":" << active_input;
  //std::cout <<  "       ---- active_input from " << from << ":" << active_input;
  auto union_tran = New<UnionTranslation>();

  while( !active_input.empty() ){
    LOG(INFO) <<"chkeck active_input string :" << active_input <<":" << active_input.size() << ":"<< active_input.empty() ;
    auto merged_tran= New<MergedTranslation>( CandidateList() );

    if (db_ && enable_predictdb_){
        LOG(INFO)<< " merged_tran : PredictDbTranslation :" <<  active_input<< segment.start<< segment.end;   
        *merged_tran += New<PredictDbTranslation>(db_, active_input, segment, initial_quality_ - 0.01);
    }
    if (translator_) {
      if (auto table_tran = translator_->Query(active_input, segment)){
        LOG(INFO)<< " merged_tran : table_translation :" <<  active_input<< segment.start<< segment.end;   
        *merged_tran += New<ShadowTranslation>(table_tran, table_shadow, active_input);
      }
    }
    *union_tran += merged_tran;
    active_input = utf8_substr(active_input, 1);
  }
  *union_tran += New<PredictDbTranslation>(db_, "$", segment, initial_quality_ -0.02); 

  LOG(INFO) << "before return translation  " << from << ":" << active_input;
  LOG(INFO) << "check trasnlation :" << union_tran->exhausted() ;
  if (union_tran && !union_tran->exhausted() ){
    auto cand = union_tran->Peek() ;
    if (cand) {
      LOG(INFO)<< "Peek first " << cand->text() ;
    }
  }
  return union_tran;
}


void PredictTranslator::load_config() {
    if (name_space_ == "translator")
      name_space_ = "predictor";


    if (auto config= engine_->schema()->config() ) {
      config->GetInt(name_space_ + "/history_length",&history_length_);
      if (history_length_ <= 0) {
        history_length_= kHistoryLength ;
      }

      config->GetString(name_space_ + "/placeholder_char", &placeholder_);

      if ( !config->GetString(name_space_ + "/tag", &tag_)){
        LOG(INFO) << "reset tag to config " << name_space_ + "/tag:" << tag_;
        config->SetString(name_space_ + "/tag", tag_);
      }
        
      config->GetBool(name_space_ + "/enable_predictdb", &enable_predictdb_);

      string prefix_from="history";
      if (config->GetString(name_space_ + "/prefix_from", &prefix_from )){
        set_prefix_from(prefix_from);
      }
      config->GetBool(name_space_ + "/text_with_prefix",&text_with_prefix_);
      config->GetBool(name_space_ + "/with_match_candidate", &with_match_candidate_);

      string dict;
      if ( config->GetString(name_space_ + "/dictionary",&dict) && !dict.empty() ) {
        if (config->IsNull(name_space_ + "/enable_completion")) {
          config->SetBool(name_space_ + "/enable_completion", true);
        }
        if (config->IsNull(name_space_ + "/enable_sentence")){
          config->SetBool(name_space_ + "/enable_sentence",false);
        }
        Ticket t{engine_, name_space_, "table_translator"};
        if ( auto c = Translator::Require(t.klass)) {
          translator_.reset(c->Create(t));
        }
      }
    }
}

// PredictTranslator
PredictTranslator::PredictTranslator(const Ticket& ticket, PredictDb* db)
  : Translator(ticket),TranslatorOptions(ticket), db_(db), placeholder_(kPlaceholder) ,
  history_length_(kHistoryLength)
{
    tag_= "prediction";
    load_config();
}


PredictTranslator::~PredictTranslator() {}

PredictTranslatorComponent::PredictTranslatorComponent() { };

PredictTranslatorComponent::~PredictTranslatorComponent() { };

Translator* PredictTranslatorComponent::Create(const Ticket& ticket) {
  if (!db_) {
    the<ResourceResolver> res(
        Service::instance().CreateResourceResolver({"predict_db", "", ""}));
    auto db = std::make_unique<PredictDb>(
        res->ResolvePath("predict.db").string());
    if (db && db->Load()) {
      db_ = std::move(db);
    }
  }
  return new PredictTranslator(ticket, db_.get());
}


}  // namespace rime
