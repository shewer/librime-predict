#ifndef RIME_PREDICTOR_H_
#define RIME_PREDICTOR_H_

#include <rime/processor.h>
//#include <rime/translator.h>

namespace rime {

class Context;
class Translator;

class Predictor : public Processor {
 public:
  Predictor(const Ticket& ticket);
  ~Predictor();

  ProcessResult ProcessKeyEvent(const KeyEvent& key_event) override;

 protected:
  void OnContextUpdate(Context* ctx);
  void OnSelect(Context* ctx);
  void Predict(Context* ctx);

 private:
  enum Action { kUnspecified, kSelect, kDelete };
  Action last_action_ = kUnspecified;
  string alphabel_;
  string placeholder_;
  an<Translator> translator_;
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
