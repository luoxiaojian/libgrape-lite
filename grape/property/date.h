#ifndef GRAPE_PROPERTY_DATE_H_
#define GRAPE_PROPERTY_DATE_H_

#include <string>

// date format:
// YYYY-MM-DD'T'hh:mm:ss.SSSZZZZ
// 2010-04-25T05:45:11.772+0000

inline static uint32_t char_to_digit(char c) {
  return (c - '0');
}

inline static uint32_t str_4_to_number(const char* str) {
  return char_to_digit(str[0]) * 1000u + char_to_digit(str[1]) * 100u + char_to_digit(str[2]) * 10u + char_to_digit(str[3]);
}

inline static uint32_t str_3_to_number(const char* str) {
  return char_to_digit(str[0]) * 100u + char_to_digit(str[1]) * 10u + char_to_digit(str[2]);
}

inline static uint32_t str_2_to_number(const char* str) {
  return char_to_digit(str[0]) * 10u + char_to_digit(str[1]);
}

inline static void number_to_str_4(uint32_t n, char* str) {
  str[0] = (n / 1000u) + '0';
  n = n % 1000u;
  str[1] = (n / 100u) + '0';
  n = n % 100u;
  str[2] = (n / 10u) + '0';
  n = n % 10u;
  str[3] = n + '0';
}

inline static void number_to_str_3(uint32_t n, char* str) {
  str[0] = (n / 100u) + '0';
  n = n % 100u;
  str[1] = (n / 10u) + '0';
  n = n % 10u;
  str[2] = n + '0';
}

inline static void number_to_str_2(uint32_t n, char* str) {
  str[0] = (n / 10u) + '0';
  n = n % 10u;
  str[1] = n + '0';
}

namespace grape {

struct Date {
  Date() = default;
  Date(const char* str) { reset(str); }
  ~Date() = default;

  void reset(const char* str) {
    year = str_4_to_number(str);
    month = str_2_to_number(&str[5]);
    day = str_2_to_number(&str[8]);

    hour = str_2_to_number(&str[11]);
    minute = str_2_to_number(&str[14]);
    second = str_2_to_number(&str[17]);
    milli_second = str_3_to_number(&str[20]);

    zone_flag = (str[23] == '+') ? 1u : 0u;
    zone_hour = str_2_to_number(&str[24]);
    zone_minute = str_2_to_number(&str[26]);
  }

  std::string to_string() const {
    std::string ret;
    ret.resize(28);

    number_to_str_4(year, &ret[0]);
    ret[4] = '-';
    number_to_str_2(month, &ret[5]);
    ret[7] = '-';
    number_to_str_2(day, &ret[8]);
    ret[10] = 'T';
    number_to_str_2(hour, &ret[11]);
    ret[13] = ':';
    number_to_str_2(minute, &ret[14]);
    ret[16] = ':';
    number_to_str_2(second, &ret[17]);
    ret[19] = '.';
    number_to_str_3(milli_second, &ret[20]);
    ret[23] = (zone_flag ? '+' : '-');
    number_to_str_2(zone_hour, &ret[24]);
    number_to_str_2(zone_minute, &ret[26]);

    return ret;
  }

  uint32_t year : 16;
  uint32_t month : 4;
  uint32_t second : 6;
  uint32_t minute : 6;
  uint32_t day : 5;
  uint32_t hour : 5;
  uint32_t milli_second: 10;
  uint32_t zone_flag: 1;
  uint32_t zone_hour : 5;
  uint32_t zone_minute: 6;
};

}  // namespace grape

#endif  // GRAPE_PROPERTY_DATE_H_