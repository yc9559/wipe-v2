#ifndef __MISC_H
#define __MISC_H

#include <string>

inline int Ms2Us(int ms) {
    return (1000 * ms);
}

inline int Mhz2kHz(int mhz) {
    return (1000 * mhz);
}

inline double Double2Pct(double d) {
    return (d * 100);
}

inline int Quantum2Ms(int n_quantum) {
    return (n_quantum * 10);
}

inline bool Replace(std::string &str, const std::string &from, const std::string &to) {
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

inline void ReplaceAll(std::string &str, const std::string &from, const std::string &to) {
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();  // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

#endif
