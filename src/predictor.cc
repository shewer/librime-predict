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

static const char kRimeAlphabel[]= "zyxwvutsrqponmlkjihgfedcba";
static const char kSelect_keys[] = "1234567890";

namespace rime {


static inline bool belongs_to(char ch, const string& charset) {
  return charset.find(ch) != std::string::npos;
}

void Predictor::LoadConfig() {
  if (Config* config = engine_->schema()->config()) {
    config->GetString("menu/alternative_select_keys", &select_keys_);
    config->GetBool(name_space_ + "/return_key_with_clear", &return_key_with_clear_);
    config->GetString(name_space_ + "/placeholder_char", &placeholder_);
    config->GetString(name_space_ + "/tips", &tips_);

    // prepare tag for table_translator
    if (config->GetString(name_space_ + "/tag", &tag_)) {
      config->SetString(name_space_ + "/tag", tag_);
    }

    // set matcher patterns
    string path = "recognizer/patterns/" + tag_;
    if (config->IsNull(path)){
      config->SetString(path ,"^" + placeholder_ + "$");
    }
    // add predict_translator
    auto conf_list = config->GetList("engine/translators");
    bool find_tran = false;
    for ( size_t i=0; i < conf_list->size() ; i++){
      auto value=conf_list->GetValueAt(i);
      if (value && value->str() == "predict_translator") {
        find_tran = true;
        break;
      }
    }
    if (! find_tran) {
      conf_list->Append(New<ConfigValue>("predict_translator")); // name_space default: predictor
    }
  }
}

Predictor::Predictor(const Ticket& ticket)
    : Processor(ticket), placeholder_(kPlaceholder),
    tag_("prediction"), select_keys_(kSelect_keys)
{
  if (name_space_ == "processor"){
    name_space_="predictor";
  }
  LoadConfig();

  select_connection_ = engine_->context()->select_notifier()
    .connect( [this](Context* ctx) { OnSelect(ctx); });
}

Predictor::~Predictor() {
  select_connection_.disconnect();
}


ProcessResult Predictor::ProcessKeyEvent(const KeyEvent& key_event) {
  auto* ctx = engine_->context();
  if (key_event.ctrl() || key_event.alt() || key_event.super()
      || ! ctx->IsComposing() || !ctx->composition().back().HasTag(tag_)) {
    return kNoop;
  }

  auto ch = key_event.keycode();

  if (ch == XK_BackSpace || ch == XK_Escape ){
    ctx->ClearPreviousSegment();
    return kAccepted;
  }
  else if ( ch == XK_Return ){
    if (!return_key_with_clear_ ) {
      ctx->ConfirmCurrentSelection();
    }
    ctx->ClearPreviousSegment();
  }
  else if ( ch >0x20 && ch <0x80 && ! belongs_to(ch, select_keys_)) {
    ctx->ClearPreviousSegment();
  }
  return kNoop;
}


void Predictor::OnSelect(Context* ctx) {
  if (ctx && ctx->get_option("predict")) {
    ctx->PushInput(placeholder_);
    ctx->composition().back().prompt = tips_;
  }
}

PredictorComponent::PredictorComponent() {};

PredictorComponent::~PredictorComponent() {};

Predictor* PredictorComponent::Create(const Ticket& ticket){
  return new Predictor(ticket);
}

}  // namespace rime
