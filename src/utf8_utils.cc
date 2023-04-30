// utf8  string tools 

#include "utf8_utils.h"


inline size_t utf8_length(const string& str){
  return  utf8::unchecked::distance(str.c_str(), str.c_str() + str.length());
}

inline static string utf8_substr_(const string& str, size_t start, size_t  len=std::string::npos)
{
    auto pos= str.begin();
    auto spos = pos;
    for (size_t s =start; s>0 && pos <str.end() ; s--) {
      utf8::unchecked::next(pos);
    }
    spos = pos;
    for ( size_t l=len; l>0 && pos <str.end()  ; l--) {
      utf8::unchecked::next(pos);
    }
    return str.substr( spos - str.begin()  , pos - spos);
}

string utf8_substr(const string& str,int start, size_t len)
{
  if (len == 0) {
    return "";
  }
  if (start < 0 ) {
    size_t ustr_len= utf8_length(str);
    start = ( (-start) >= ustr_len  ) ? 0 :   ustr_len + start;
  }
  return utf8_substr_(str, start,len);
}

