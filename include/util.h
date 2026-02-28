#ifndef UTIL_H
#define UTIL_H

#include <string>

inline int utf8CharLen(const std::string& s, int i) {
    unsigned char c = s[i];
    if(c < 0x80) return 1;
    if(c < 0xE0) return 2;
    if(c < 0xF0) return 3;
    return 4;
}

inline int utf8PrevCharLen(const std::string& s, int i) {
    int len = 1;
    while(len < i && (s[i - len] & 0xC0) == 0x80) len++;
    return len;
}

inline bool isHeaderMarker(const std::string& s, int i) {
    return i + 2 < (int)s.size() && s[i] == '#' &&
           (s[i+1] == '1' || s[i+1] == '2' || s[i+1] == '3') &&
           s[i+2] == ' ';
}

#endif
