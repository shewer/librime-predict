## 2023/05/18
* reconfig recoginzer/pattern/<tag>:  ^< placeholder_char>$ for matcher
  auto insert predict_translator in engine/translators
* 修正 predict_translator candidate preedit 長度,解決pos 不一致造成崩潰
* 增加 dbpool[dbname] , <name_space>/predictdb: dbname 預設 "predict"


## 2023/04/18
* history_text : 設定 累加utf8字串最大值
* UnionTranslation 加入 PredictDb history_text(max) --> 0
* UnionTranslation 右入 essay_translation 使用 PrefetchTranslation (shadowCandidate)
* Escape BackSpace 中斷 prediction 模式
* 增加 speller/alphabet: zyxwvutsrqponmlkjihgfedcba 離開 prediction


  ### 安裝方案:

   * cp essay*  user_data_dir/.
   * cp predict_plugin.yaml user_dict_dir/.
   * editor  <schema>.custom.yaml

      ```yaml
      #cangjie5.custom.yaml


      __patch:
        - patch/+:
           __include: predict_plugin:/patch

       ```
  ### yaml  設定參數

  ```yaml
  # predict_plugin.yaml
  ## yaml config
  patch:
    #<name_space>:
    engine/processors/@after 1: predictor # add procssor beforre punt speller
    predictor:
      enabel_predictdb: false # 引用 predictdb default true
      #tag:             # 預設 prediction 判定 segment 起動 compose
      prefix_from: history|property|input #設定 input 輸入來源 預設 commit_history ，使用 input property 可供librime-lua 應用
      history_length: 7 # 設定 prefix 最大長度(utf8 code) 預設 3
      initial_quality:  # 設定 candidate quality 值

      dictionary: essay # 引用 table_translator :essay dict 如果無設定或空白則不引用 table_translatior
      enabel_completion: true  # 必須設定 true
      enable_sentence: false   # 必須設定 false
      enable_user_dict: true



```

 ## essay.dict.yaml  製作格式

  essay.dict.yaml 格式 (table.bin)
  我\t我\t <weight>
  ...

  ProcessKeyEvent()

   kActive

    ascii  execpt select_key and space
     clearpreviousSegment() ; kNoop

   BackSpace Escape
     kNoop

   Return  : use setup kNoop or clear
     kNoop
   Return
     clearpeviouSegment() ; kNoop


   BackSpace    Escape  space

   Return




