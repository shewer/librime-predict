#ifndef RIME_PREDICTOR_H_
#define RIME_PREDICTOR_H_

#include <rime/processor.h>
//#include <rime/translator.h>

namespace rime {

class Context;
class Translator;

class Predictor : public Processor {
 public:
  enum Action { kStop, kUnspecified, kSelect, kActive};
  Predictor(const Ticket& ticket);
  ~Predictor();

  ProcessResult ProcessKeyEvent(const KeyEvent& key_event) override;

 protected:
  void OnSelect(Context* ctx);
  void LoadConfig();

 private:
  Action status_= kUnspecified;
  bool active_= false;
  bool disable_update_=false;
  string tag_;
  bool return_key_with_clear_ = true;
  string select_keys_;
  string placeholder_;
  an<Translator> translator_;
  connection commit_connection_;
  connection select_connection_;
  connection context_update_connection_;
};

class PredictorComponent : public Predictor::Component {
 public:
  PredictorComponent();
  virtual ~PredictorComponent();
  Predictor* Create(const Ticket& ticket) override;

 protected:
};
}  // namespace rime

#endif  // RIME_PREDICTOR_H_
