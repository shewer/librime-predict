#include "predictor.h"
#include "predict_translator.h"

#include <rime/context.h>
#include <rime/engine.h>
#include <rime/key_event.h>
#include <rime/menu.h>
//#include <rime/segmentation.h>
#include <rime/service.h>
#include <rime/schema.h>
//#include <rime/config.h>
#include <rime/translator.h>
#include <rime/translation.h>


static const char* kPlaceholder = " ";
static const char kRimeAlphabel[]= "zyxwvutsrqponmlkjihgfedcba";

namespace rime {

static inline bool belongs_to(char ch, const string& charset) {
  return charset.find(ch) != std::string::npos;
}

Predictor::Predictor(const Ticket& ticket)
    : Processor(ticket), alphabel_(kRimeAlphabel), placeholder_(kPlaceholder) {
  if (name_space_ == "processor"){
    name_space_="predictor";
  }
  if (Config* config = engine_->schema()->config()) {
    config->GetString("speller/alphabel", &alphabel_);
    config->GetString(name_space_ + "/placeholder_char", &placeholder_);
  }
  if (auto c = Translator::Require("predict_translator")){
    translator_.reset( c->Create( Ticket(engine_, name_space_, "predict_translator")) );
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
  if ( ch == XK_BackSpace or ch == XK_Escape) {
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
    last_action_ = kSelect;
}

void Predictor::OnContextUpdate(Context* ctx) {
  if (!translator_ || !ctx || !ctx->composition().empty())
    return;
  if (last_action_ == kDelete) {
    return;
  }
  if (ctx->get_option("predict")){
    Predict(ctx);
  }
}

void Predictor::Predict(Context* ctx) {
  ctx->set_input(placeholder_);
  int end = ctx->input().length();
  Segment segment(0,end);
  segment.status = Segment::kGuess;
  segment.tags.insert("prediction");
  auto comp = ctx->composition();
  comp.AddSegment(segment);
  comp.back().tags.erase("raw");

  auto menu = New<Menu>();
  menu->AddTranslation( translator_->Query(ctx->input(),segment));
  ctx->composition().back().menu = menu;
}

PredictorComponent::PredictorComponent() {};

PredictorComponent::~PredictorComponent() {};

Predictor* PredictorComponent::Create(const Ticket& ticket){
  return new Predictor(ticket);
}

}  // namespace rime
