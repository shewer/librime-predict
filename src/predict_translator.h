/*
 * predict_translator.h
 * Copyright (C) 2023 Shewer Lu <shewer@gmail.com>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef PREDICT_TRANSLATOR_H
#define PREDICT_TRANSLATOR_H
#include <rime/translator.h>
#include <rime/gear/translator_commons.h>

namespace rime {
static const char* kPlaceholder = " ";
static const int kHistoryLength = 3;

class Context;
class PredictDb;
class Translation;
class TranslatorOptions;

class PredictTranslator : public Translator, public TranslatorOptions {
  public:
    PredictTranslator(const Ticket& ticket, PredictDb* db);
    ~PredictTranslator();
    an<Translation> Query(const string& input, const Segment& segment );

  private:
    enum Prefix_from{ kHistory,kProperty, kInput};
    Prefix_from prefix_from_ = kHistory;
    void set_prefix_from(const string& str);
    void set_prefix_from(const Prefix_from prefix_from) { prefix_from_= prefix_from; };
    void load_config();

    bool text_with_prefix_= false;
    bool with_match_candidate_= false;
    bool enable_predictdb_=true;
    PredictDb* db_;
    string placeholder_;
    the<Translator> translator_;
    int history_length_;

};


class PredictTranslatorComponent : public PredictTranslator::Component {
 public:
  PredictTranslatorComponent();
  virtual ~PredictTranslatorComponent();
  Translator* Create(const Ticket& ticket) override;

 protected:
  the<PredictDb> db_;
};
}  // namespace rime
#endif /* !PREDICT_TRANSLATOR_H */
