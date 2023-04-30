/*
 * utf8_utils.h
 * Copyright (C) 2023 Shewer Lu <shewer@gmail.com>
 *
 * Distributed under terms of the MIT license.
 */

#ifndef UTF8_UTILS_H
#define UTF8_UTILS_H

#include <utf8.h>

using std::string;
inline size_t utf8_lenght(const string& str){
  return  utf8::unchecked::distance(str.c_str(), str.c_str() + str.length());
}
string utf8_substr(const string& str,int start, size_t len= std::string::npos);

#endif /* !UTF8_UTILS_H */
