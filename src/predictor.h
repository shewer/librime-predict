#ifndef RIME_PREDICTOR_H_
#define RIME_PREDICTOR_H_

#include <rime/processor.h>

namespace rime {

class Context;
class Translator;

class Predictor : public Processor {
 public:
  Predictor(const Ticket& ticket);
  ~Predictor();

  ProcessResult ProcessKeyEvent(const KeyEvent& key_event) override;

 protected:
  void OnSelect(Context* ctx);
  void LoadConfig();

 private:
  string tag_;
  string tips_;
  bool return_key_with_clear_ = true;
  string select_keys_;
  string placeholder_;
  connection select_connection_;
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
