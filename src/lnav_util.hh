/**
 * Copyright (c) 2007-2012, Timothy Stack
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * * Neither the name of Timothy Stack nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ''AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @file lnav_util.hh
 *
 * Dumping ground for useful functions with no other home.
 */

#ifndef __lnav_util_hh
#define __lnav_util_hh

#include <time.h>
#include <sys/types.h>

#include <openssl/sha.h>

#include <string>

#include "byte_array.hh"

#undef rounddown

/**
 * Round down a number based on a given granularity.
 *
 * @param
 * @param step The granularity.
 */
inline int rounddown(size_t size, int step)
{
    return size - (size % step);
}

inline int rounddown_offset(size_t size, int step, int offset)
{
    return size - ((size - offset) % step);
}

inline int roundup_size(size_t size, int step)
{
    int retval = size + step;

    retval -= (retval % step);

    return retval;
}

inline time_t day_num(time_t ti)
{
    return ti / (24 * 60 * 60);
}

inline time_t hour_num(time_t ti)
{
    return ti / (60 * 60);
}

std::string time_ago(time_t last_time);

#if SIZEOF_OFF_T == 8
#define FORMAT_OFF_T    "%qd"
#elif SIZEOF_OFF_T == 4
#define FORMAT_OFF_T    "%ld"
#else
#error "off_t has unhandled size..."
#endif

struct sha_updater {
    sha_updater(SHA_CTX *context) : su_context(context) { };

    void operator()(const std::string &str)
    {
        SHA_Update(this->su_context, str.c_str(), str.length());
    }

    SHA_CTX *su_context;
};

std::string hash_string(const std::string &str);

template<typename UnaryFunction, typename Member>
struct object_field_t {
    object_field_t(UnaryFunction &func, Member &mem)
        : of_func(func), of_mem(mem) {};

    template<typename Object>
    void operator()(Object obj)
    {
        this->of_func(obj.*(this->of_mem));
    };

    UnaryFunction &of_func;
    Member         of_mem;
};

template<typename UnaryFunction, typename Member>
object_field_t<UnaryFunction, Member> object_field(UnaryFunction &func,
                                                   Member mem)
{
    return object_field_t<UnaryFunction, Member>(func, mem);
}

/* XXX figure out how to do this with the template */
void sqlite_close_wrapper(void *mem);

std::string get_current_dir(void);

bool change_to_parent_dir(void);

enum file_format_t {
    FF_UNKNOWN,
    FF_SQLITE_DB,
};

file_format_t detect_file_format(const std::string &filename);

bool next_format(const char *fmt[], int &index, int &locked_index);

inline bool is_glob(const char *fn)
{
    return (strchr(fn, '*') != NULL ||
            strchr(fn, '?') != NULL ||
            strchr(fn, '[') != NULL);
};

/**
 * Convert the time stored in a 'tm' struct into epoch time.
 *
 * @param t The 'tm' structure to convert to epoch time.
 * @return The given time in seconds since the epoch.
 */
time_t tm2sec(const struct tm *t);

extern const char *std_time_fmt[];

struct date_time_scanner {
    date_time_scanner() : dts_local_time(false) {
        this->clear();
    };

    void clear(void) {
        this->dts_base_time = 0;
        memset(&this->dts_base_tm, 0, sizeof(this->dts_base_tm));
        this->dts_fmt_lock = -1;
        this->dts_fmt_len = -1;
    };

    void set_base_time(time_t base_time) {
        this->dts_base_time = base_time;
        localtime_r(&base_time, &this->dts_base_tm);
    };

    bool dts_local_time;
    time_t dts_base_time;
    struct tm dts_base_tm;
    int dts_fmt_lock;
    int dts_fmt_len;

    const char *scan(const char *time_src,
                     const char *time_fmt[],
                     struct tm *tm_out,
                     struct timeval &tv_out);
};

#endif
