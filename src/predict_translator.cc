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

#define kHistoryLength  (3)

//translation
//query
//  switch input from  history input property
//  predictdb  db active_input
//
//  switch input from  history input property
//
//  with_candidate (has_menu)
//  with_match_text (cand.text == active_input)
//  table  active_input and wrap shadow
//
namespace rime {


class PredictDbTranslation : public FifoTranslation {
  public:
    PredictDbTranslation(PredictDb *db, const string prefix,
        size_t start, size_t end, TranslatorOptions* options);
    ~PredictDbTranslation() {};
  protected:
    PredictDb* db_;
    string prefix_;
    size_t start_;
    size_t end_;
    TranslatorOptions* options_;

};

PredictDbTranslation::PredictDbTranslation(PredictDb *db, const string prefix
    , size_t start,size_t end ,TranslatorOptions* t)
  : db_(db), prefix_(prefix), start_(start),end_(end), options_(t) {

  if (auto cands = db_->Lookup(prefix)) {
    string comment= "pre:" + prefix_;
    string preedit= prefix_;
    options_->comment_formatter().Apply(&preedit);
    options_->preedit_formatter().Apply(&comment);

    for (auto* it=cands->begin() ; it != cands->end() ; it++) {
      auto cand =New<SimpleCandidate>("prediction", start_, end_
          , db_->GetEntryText(*it), comment, preedit);
      cand->set_quality(options_->initial_quality()-0.00001);
      Append(cand);
    }
  }
}

typedef std::function<an<Translation> (const string&)> predict_func;

class PredictTranslation : public Translation {
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
  : prefix_(prefix), func_(func) {
  set_exhausted( Replenish() );
}

bool PredictTranslation::Next() {
  if(exhausted()) {
    return false;
  }
  if (translation_->Next()) {
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

bool PredictTranslation::Replenish() {
  if (prefix_.empty()) {
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

  if (!translation_ || translation_->exhausted()) {
    return Replenish();
  }
  return true;
}


class PredictTranslation1 : public UnionTranslation{
  public:
    PredictTranslation1( const string& prefix, predict_func func)
      : prefix_(prefix), func_(func) { Replenish() ; };
    //bool Next();
    //an<Candidate> Peek();
  protected:
    bool Replenish();
    string prefix_;
    predict_func func_;
};

bool PredictTranslation1::Replenish() {
  do {
    auto t = func_(prefix_); // <<---------- Bug 找不到時會異常

    if (t && !t->exhausted()) {
      *this += t;
    }
    string res = utf8_substr(prefix_, 1);
    prefix_ = (res.length() == 0 && prefix_ != "$") ? "$" : res;
  }while( !prefix_.empty()  );
  return !exhausted();
}


// ShadowTranslation
typedef std::function<an<Candidate>(an<Candidate> cand, const size_t start, const size_t end, const string&, const string&)> shadow_func;
// ShadowTranslation  table candidate to predict candidate
an<Candidate> predict_shadow(an<Candidate> cand, const string& prefix, const string& last_match) {
  size_t f_idx = cand->text().find(prefix);
  if (f_idx == 0 && cand->text().length() >= prefix.length() ) {
    auto text = cand->text().substr(prefix.length());
    double fix_quality = (cand->type()=="completion") ? 1.0 : 0;
    cand->set_quality( fix_quality + cand->quality());
    return New<ShadowCandidate>(
        cand,cand->type(), text,"tab",false);
  }
  return {};
}
an<Candidate> table_shadow(an<Candidate> cand, const size_t start, const size_t end
    , const string& prefix, const string& last_match) {
  auto sub_str = prefix + last_match;
  if (!cand || cand->type() != "completion"
      || sub_str.length() >= cand->text().length()
      || cand->text().find(sub_str) != 0 ) {
    return nullptr;
  }

  // substr from prefix
  auto text = cand->text().substr( sub_str.length());
  cand->set_start(start);
  cand->set_end(end);
  //double fix_quality = (cand->type()=="completion") ? 1.0 : 0;
  cand->set_quality( 1.0 + cand->quality());
  return New<ShadowCandidate>( cand,"prediction-tab", text,"tab",false);
}

an<Candidate> predict_shadow2(an<Candidate> cand, const string& last_match) {
  return {};
}
class ShadowTranslation : public CacheTranslation{
  public:
    ShadowTranslation(an<Translation> translation
        , shadow_func func, const size_t start, const size_t end
        , const string& prefix, const string& last_matcm = "" );
    an<Candidate> Peek();
  protected:
    size_t start_;
    size_t end_;
    string prefix_;
    string last_match_;
    shadow_func func_;
};


ShadowTranslation::ShadowTranslation(an<Translation> translation, shadow_func func
    , const size_t start, const size_t end, const string& prefix, const string& last_match)
  : CacheTranslation(translation), prefix_(prefix), start_(start), end_(end)
    , last_match_(last_match), func_(func) {
    // prepare first candidate of type of completion
    Peek();
}


an<Candidate> ShadowTranslation::Peek() {
  if (exhausted())
    return nullptr;
  if (cache_)
    return cache_;

  do {
    if (auto cand = func_(translation_->Peek(), start_, end_, prefix_, last_match_)) {
        cache_ = cand;
        return cache_;
    }
  } while( Next() );
  return nullptr;
}

void PredictTranslator::set_prefix_from(const string& str) {
  if (str == "history")
    prefix_from_ =kHistory;
  else if (str == "property")
    prefix_from_ = kProperty;
  else if (str == "input")
    prefix_from_ = kInput;
  else {
    LOG(WARNING) << "prefix_from:" << str << "not found(set default:history";
    prefix_from_ = kHistory;
  }
}

static string get_history_text(Context* ctx, size_t len) {
  string text;
  if (ctx->IsComposing() ) {
    auto comp = ctx->composition();
    for (auto it=comp.rbegin(); it != comp.rend(); it++) {
      if (it->status < Segment::kSelected ) {
        continue;
      }
      if (auto cand = it->GetSelectedCandidate()) {
        //text.insert(0,cand->text());
        string res = utf8_substr( text.insert(0, cand->text()) , -(len));
        if (utf8_length(res) >= len )  {
          return res;
        }
      }
    }
  }
  // get commit_text from commit_history
  auto cm=ctx->commit_history();
  if (cm.empty() && text.empty() ) {
    //return "$"; // ::Query already supply "$"
    return text;
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
      if (utf8_length(res) >= len )  {
        return res;
      }
    }
  }
  return text;
}

an<Translation> PredictTranslator::Query(const string& input, const Segment& segment ) {
  if ( !segment.HasTag(tag_) ) {
    return nullptr;
  }

  // init active_input
  string active_input;
  string from;
  Context* ctx= engine_->context();
  if (prefix_from_== kHistory) {
     from = "history";
     active_input= get_history_text(ctx, history_length_);
  }
  else if (prefix_from_ == kProperty) {
    from = "property";
    active_input = utf8_substr( ctx->get_property("prediction") ,-(history_length_));
  }
  else if (prefix_from_ == kInput) {
    from = "input";
    active_input = input;
  }
  else{
    return nullptr;
  }
  LOG(INFO) << "active_input from " << from << ":" << active_input;
  auto union_tran = New<UnionTranslation>();

  while( !active_input.empty() ) {
    auto merged_tran= New<MergedTranslation>( CandidateList() );

    if (db_ && enable_predictdb_) {
        *merged_tran += New<PredictDbTranslation>(db_, active_input
            , segment.start, segment.end, this);
    }
    if (translator_) {
      if (auto table_tran = translator_->Query(active_input, segment)) {
        // reset  cand.start cand.end and substr
        *merged_tran += New<ShadowTranslation>(table_tran, table_shadow
            , segment.start , segment.end, active_input);
      }
    }
    *union_tran += merged_tran;

    if (active_input == "$") {
      break;
    }
    active_input = utf8_substr(active_input, 1);
    if ( active_input.empty() ) {
      active_input = "$";
    }
  }
  return union_tran;
}

void PredictTranslator::LoadConfig() {
  if (name_space_ == "translator") {
    name_space_ = "predictor";
  }
  if (tag_ == "abc") {
    tag_ = "prediction";
  }

  if (auto config= engine_->schema()->config() ) {
    config->GetInt(name_space_ + "/history_length",&history_length_);
    if (history_length_ <= 0) {
      history_length_= kHistoryLength ;
    }

    config->GetString(name_space_ + "/placeholder_char", &placeholder_);
    config->GetBool(name_space_ + "/enable_predictdb", &enable_predictdb_);
    // prepare tag for table_translator
    if (!config->GetString(name_space_ + "/tag", &tag_)) {
      config->SetString(name_space_ + "/tag", tag_);
    }

    string prefix_from="history";
    if (config->GetString(name_space_ + "/prefix_from", &prefix_from )) {
      set_prefix_from(prefix_from);
    }
    config->GetBool(name_space_ + "/text_with_prefix",&text_with_prefix_);
    config->GetBool(name_space_ + "/with_match_candidate", &with_match_candidate_);

    string dict;
    if ( config->GetString(name_space_ + "/dictionary",&dict) && !dict.empty() ) {
      if (config->IsNull(name_space_ + "/enable_completion")) {
        config->SetBool(name_space_ + "/enable_completion", true);
      }
      if (config->IsNull(name_space_ + "/enable_sentence")) {
        config->SetBool(name_space_ + "/enable_sentence",false);
      }
      if ( auto c = Translator::Require("table_translator")) {
        translator_.reset( c->Create( Ticket(engine_, name_space_, "table_translator") ));
      }
    }
  }
}

// PredictTranslator
PredictTranslator::PredictTranslator(const Ticket& ticket, PredictDb* db)
  : Translator(ticket),TranslatorOptions(ticket), db_(db)
    , placeholder_(kPlaceholder), history_length_(kHistoryLength)
{
    LoadConfig();
}


PredictTranslator::~PredictTranslator() {}

PredictTranslatorComponent::PredictTranslatorComponent() {};

PredictTranslatorComponent::~PredictTranslatorComponent() {};

Translator* PredictTranslatorComponent::Create(const Ticket& ticket) {
  if (! ticket.schema) {
    return NULL;
  }

  string ns = ("translator" == ticket.name_space ) ? "predictor" : ticket.name_space;
  string dbname = "predict";
  if (auto config = ticket.schema->config()) {
    config->GetString( ns + "/predictdb", &dbname);
  }

  if (!dbpool_[dbname]) {
    the<ResourceResolver> res(
        Service::instance().CreateResourceResolver({"predict_db", "", ""}));

    auto db = std::make_unique<PredictDb>(
        res->ResolvePath(dbname + ".db").string());
    if (db && db->Load()) {
      LOG(INFO) << "load PredictDb : " << dbname  << ".db";
      dbpool_[dbname] = std::move(db);
    }
    else {
      LOG(WARNING) << "PredictDb not fount:" << ticket.klass << "@"
        << ns << ":/predictdb: "<< dbname <<".db";
    }
  }

  LOG(INFO) << "create Component: predictor:" << ticket.klass << "@" << ns;
  return new PredictTranslator( Ticket(ticket.engine, ns, ticket.klass)
      , dbpool_[dbname].get());
}

}  // namespace rime
