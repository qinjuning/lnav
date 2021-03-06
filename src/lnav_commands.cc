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
 */

#include "config.h"

#include <wordexp.h>
#include <sys/stat.h>

#include <string>
#include <vector>
#include <fstream>

#include <pcrecpp.h>

#include "lnav.hh"
#include "lnav_config.hh"
#include "lnav_util.hh"
#include "auto_mem.hh"
#include "log_data_table.hh"
#include "lnav_commands.hh"

using namespace std;

static string com_adjust_log_time(string cmdline, vector<string> &args)
{
    string retval = "error: expecting new time value";

    if (args.size() == 0) {
        args.push_back("line-time");
    }
    else if (lnav_data.ld_views[LNV_LOG].get_inner_height() == 0) {
        retval = "error: no log messages";
    }
    else if (args.size() >= 2) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        struct timeval top_time, time_diff;
        struct timeval new_time = { 0, 0 };
        content_line_t top_content;
        date_time_scanner dts;
        vis_line_t top_line;
        struct tm tm;
        logfile *lf;

        top_line = lnav_data.ld_views[LNV_LOG].get_top();
        top_content = lss.at(top_line);
        lf = lss.find(top_content);

        logline &ll = (*lf)[top_content];

        top_time = ll.get_timeval();

        dts.set_base_time(top_time.tv_sec);
        args[1] = cmdline.substr(cmdline.find(args[1]));
        if (dts.scan(args[1].c_str(), NULL, &tm, new_time) != NULL) {
            timersub(&new_time, &top_time, &time_diff);
            
            lf->adjust_content_time(top_content, time_diff, false);

            rebuild_indexes(true);

            retval = "info: adjusted time";
        }
    }

    return retval;
}

static string com_unix_time(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a unix time value";

    if (args.size() == 0) { }
    else if (args.size() >= 2) {
        char      ftime[128] = "";
        bool      parsed     = false;
        struct tm log_time;
        time_t    u_time;
        size_t    millis;
        char *    rest;

        u_time   = time(NULL);
        log_time = *localtime(&u_time);

        log_time.tm_isdst = -1;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        if ((millis = args[1].find('.')) != string::npos ||
            (millis = args[1].find(',')) != string::npos) {
            args[1] = args[1].erase(millis, 4);
        }
        if (((rest = strptime(args[1].c_str(),
                              "%b %d %H:%M:%S %Y",
                              &log_time)) != NULL &&
             (rest - args[1].c_str()) >= 20) ||
            ((rest = strptime(args[1].c_str(),
                              "%Y-%m-%d %H:%M:%S",
                              &log_time)) != NULL &&
             (rest - args[1].c_str()) >= 19)) {
            u_time = mktime(&log_time);
            parsed = true;
        }
        else if (sscanf(args[1].c_str(), "%ld", &u_time)) {
            log_time = *localtime(&u_time);

            parsed = true;
        }
        if (parsed) {
            int len;

            strftime(ftime, sizeof(ftime),
                     "%a %b %d %H:%M:%S %Y  %z %Z",
                     localtime(&u_time));
            len = strlen(ftime);
            snprintf(ftime + len, sizeof(ftime) - len,
                     " -- %ld\n",
                     u_time);
            retval = string(ftime);
        }
    }

    return retval;
}

static string com_current_time(string cmdline, vector<string> &args)
{
    char      ftime[128];
    struct tm localtm;
    string    retval;
    time_t    u_time;
    int       len;

    memset(&localtm, 0, sizeof(localtm));
    u_time = time(NULL);
    strftime(ftime, sizeof(ftime),
             "%a %b %d %H:%M:%S %Y  %z %Z",
             localtime_r(&u_time, &localtm));
    len = strlen(ftime);
    snprintf(ftime + len, sizeof(ftime) - len,
             " -- %ld\n",
             u_time);
    retval = string(ftime);

    return retval;
}

static string com_goto(string cmdline, vector<string> &args)
{
    string retval = "error: expecting line number/percentage";

    if (args.size() == 0) { }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        int   line_number, consumed;
        float value;

        if (sscanf(args[1].c_str(), "%f%n", &value, &consumed) == 1) {
            if (args[1][consumed] == '%') {
                line_number = (int)
                              ((double)tc->get_inner_height() *
                               (value / 100.0));
            }
            else {
                line_number = (int)value;
            }
            tc->set_top(vis_line_t(line_number));

            retval = "";
        }
    }

    return retval;
}

static bool csv_needs_quoting(const string &str)
{
    return (str.find_first_of(",\"") != string::npos);
}

static string csv_quote_string(const string &str)
{
    static pcrecpp::RE csv_column_quoter("\"");

    string retval = str;

    csv_column_quoter.GlobalReplace("\"\"", &retval);
    retval.insert(0, 1, '\"');
    retval.append(1, '\"');

    return retval;
}

static void csv_write_string(FILE *outfile, const string &str)
{
    if (csv_needs_quoting(str)) {
        string quoted_str = csv_quote_string(str);

        fprintf(outfile, "%s", quoted_str.c_str());
    }
    else {
        fprintf(outfile, "%s", str.c_str());
    }
}

static string com_save_to(string cmdline, vector<string> &args)
{
    FILE *      outfile = NULL;
    const char *mode    = "";

    if (args.size() == 0) {
        args.push_back("filename");
        return "";
    }

    if (args.size() != 2) {
        return "error: expecting file name";
    }

    static_root_mem<wordexp_t, wordfree> wordmem;

    switch (wordexp(args[1].c_str(), wordmem.inout(), WRDE_NOCMD |
                    WRDE_UNDEF)) {
    case WRDE_BADCHAR:
        return "error: invalid filename character";

    case WRDE_CMDSUB:
        return "error: command substitution is not allowed";

    case WRDE_BADVAL:
        return "error: unknown environment variable in file name";

    case WRDE_NOSPACE:
        return "error: out of memory";

    case WRDE_SYNTAX:
        return "error: invalid syntax";

    default:
        break;
    }

    if (wordmem->we_wordc > 1) {
        return "error: more than one file name was matched";
    }

    if (args[0] == "append-to") {
        mode = "a";
    }
    else if (args[0] == "write-to" || args[0] == "write-csv-to") {
        mode = "w";
    }


    textview_curses *            tc = lnav_data.ld_view_stack.top();
    bookmark_vector<vis_line_t> &bv =
        tc->get_bookmarks()[&textview_curses::BM_USER];
    db_label_source &dls = lnav_data.ld_db_rows;

    if (args[0] == "write-csv-to") {
        if (dls.dls_headers.empty()) {
            return "error: no query result to write, use ';' to execute a query";
        }
    }
    else {
        if (bv.empty()) {
            return "error: no lines marked to write, use 'm' to mark lines";
        }
    }

    if ((outfile = fopen(wordmem->we_wordv[0], mode)) == NULL) {
        return "error: unable to open file -- " + string(wordmem->we_wordv[0]);
    }

    if (args[0] == "write-csv-to") {
        std::vector<std::vector<string> >::iterator row_iter;
        std::vector<string>::iterator iter;
        bool first = true;

        for (iter = dls.dls_headers.begin();
             iter != dls.dls_headers.end();
             ++iter) {
            if (!first) {
                fprintf(outfile, ",");
            }
            csv_write_string(outfile, *iter);
            first = false;
        }
        fprintf(outfile, "\n");

        for (row_iter = dls.dls_rows.begin();
             row_iter != dls.dls_rows.end();
             ++row_iter) {
            first = true;
            for (iter = row_iter->begin();
                 iter != row_iter->end();
                 ++iter) {
                if (!first) {
                    fprintf(outfile, ",");
                }
                csv_write_string(outfile, *iter);
                first = false;
            }
            fprintf(outfile, "\n");
        }
    }
    else {
        bookmark_vector<vis_line_t>::iterator iter;
        string line;

        for (iter = bv.begin(); iter != bv.end(); iter++) {
            tc->grep_value_for_line(*iter, line);
            fprintf(outfile, "%s\n", line.c_str());
        }
    }

    fclose(outfile);
    outfile = NULL;

    return "";
}

static string com_highlight(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to highlight";

    if (args.size() == 0) { }
    else if (args.size() > 1) {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        textview_curses::highlight_map_t &hm = tc->get_highlights();
        const char *errptr;
        pcre *      code;
        int         eoff;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        if (hm.find(args[1]) != hm.end()) {
            retval = "error: highlight already exists";
        }
        else if ((code = pcre_compile(args[1].c_str(),
                                      PCRE_CASELESS,
                                      &errptr,
                                      &eoff,
                                      NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            textview_curses::highlighter hl(code, false);

            hm[args[1]] = hl;

            retval = "info: highlight pattern now active";
        }
    }

    return retval;
}

static string com_graph(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to graph";

    if (args.size() == 0) {
        args.push_back("graph");
    }
    else if (args.size() > 1) {
        const char *errptr;
        pcre *      code;
        int         eoff;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        if ((code = pcre_compile(args[1].c_str(),
                                 PCRE_CASELESS,
                                 &errptr,
                                 &eoff,
                                 NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            textview_curses &            tc = lnav_data.ld_views[LNV_LOG];
            textview_curses::highlighter hl(code, true);

            textview_curses::highlight_map_t &hm = tc.get_highlights();

            hm["(graph"] = hl;
            lnav_data.ld_graph_source.set_highlighter(&hm["(graph"]);

            auto_ptr<grep_proc> gp(new grep_proc(code,
                                                 tc,
                                                 lnav_data.ld_max_fd,
                                                 lnav_data.ld_read_fds));

            gp->queue_request();
            gp->start();
            gp->set_sink(&lnav_data.ld_graph_source);

            auto_ptr<grep_highlighter>
            gh(new grep_highlighter(gp, "(graph", hm));
            lnav_data.ld_grep_child[LG_GRAPH] = gh;

            toggle_view(&lnav_data.ld_views[LNV_GRAPH]);

            retval = "";
        }
    }

    return retval;
}

static string com_help(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {}
    else {
        ensure_view(&lnav_data.ld_views[LNV_HELP]);
    }

    return retval;
}

class pcre_filter
    : public logfile_filter {
public:
    pcre_filter(type_t type, string id, pcre *code)
        : logfile_filter(type, id),
          pf_code(code) { };
    virtual ~pcre_filter() { };

    bool matches(string line)
    {
        static const int MATCH_COUNT = 20 * 3;
        int  matches[MATCH_COUNT], rc;
        bool retval;

        rc = pcre_exec(this->pf_code,
                       NULL,
                       line.c_str(),
                       line.size(),
                       0,
                       0,
                       matches,
                       MATCH_COUNT);
        retval = (rc >= 0);

#if 0
        fprintf(stderr, " out %d %s\n",
                retval,
                line.c_str());
#endif

        return retval;
    };

    std::string to_command(void)
    {
        return (this->lf_type == logfile_filter::INCLUDE ?
                "filter-in " : "filter-out ") +
               this->lf_id;
    };

protected:
    auto_mem<pcre> pf_code;
};

static string com_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting regular expression to filter out";

    if (args.size() == 0) {
        args.push_back("filter");
    }
    else if (args.size() > 1) {
        logfile_sub_source &lss = lnav_data.ld_log_source;
        const char *errptr;
        pcre *      code;
        int         eoff;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        if (lss.get_filter(args[1]) != NULL) {
            retval = "error: filter already exists";
        }
        else if ((code = pcre_compile(args[1].c_str(),
                                      0,
                                      &errptr,
                                      &eoff,
                                      NULL)) == NULL) {
            retval = "error: " + string(errptr);
        }
        else {
            logfile_filter::type_t lt  = (args[0] == "filter-out") ?
                                         logfile_filter::EXCLUDE :
                                         logfile_filter::INCLUDE;
            auto_ptr<pcre_filter> pf(new pcre_filter(lt, args[1], code));

            lss.add_filter(pf.release());
            lnav_data.ld_rl_view->
            add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
            rebuild_indexes(true);

            retval = "info: filter now active";
        }
    }

    return retval;
}

static string com_enable_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting disabled filter to enable";

    if (args.size() == 0) {
        args.push_back("disabled-filter");
    }
    else if (args.size() > 1) {
        logfile_filter *lf;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        lf      = lnav_data.ld_log_source.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (lf->is_enabled()) {
            retval = "info: filter already enabled";
        }
        else {
            lnav_data.ld_log_source.set_filter_enabled(lf, true);
            lnav_data.ld_rl_view->
            rem_possibility(LNM_COMMAND, "disabled-filter", args[1]);
            lnav_data.ld_rl_view->
            add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
            rebuild_indexes(true);
            retval = "info: filter enabled";
        }
    }

    return retval;
}

static string com_disable_filter(string cmdline, vector<string> &args)
{
    string retval = "error: expecting enabled filter to disable";

    if (args.size() == 0) {
        args.push_back("enabled-filter");
    }
    else if (args.size() > 1) {
        logfile_filter *lf;

        args[1] = cmdline.substr(cmdline.find(args[1]));
        lf      = lnav_data.ld_log_source.get_filter(args[1]);
        if (lf == NULL) {
            retval = "error: no such filter -- " + args[1];
        }
        else if (!lf->is_enabled()) {
            retval = "info: filter already disabled";
        }
        else {
            lnav_data.ld_log_source.set_filter_enabled(lf, false);
            lnav_data.ld_rl_view->
            rem_possibility(LNM_COMMAND, "disabled-filter", args[1]);
            lnav_data.ld_rl_view->
            add_possibility(LNM_COMMAND, "enabled-filter", args[1]);
            rebuild_indexes(true);
            retval = "info: filter disabled";
        }
    }

    return retval;
}

static std::vector<string> custom_logline_tables;

static string com_create_logline_table(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.size() == 0) {}
    else if (args.size() == 2) {
        textview_curses &log_view = lnav_data.ld_views[LNV_LOG];

        if (log_view.get_inner_height() == 0) {
            retval = "error: no log data available";
        }
        else {
            vis_line_t      vl  = log_view.get_top();
            content_line_t  cl  = lnav_data.ld_log_source.at(vl);
            log_data_table *ldt = new log_data_table(cl, args[1]);
            string          errmsg;

            errmsg = lnav_data.ld_vtab_manager->register_vtab(ldt);
            if (errmsg.empty()) {
                lnav_data.ld_rl_view->add_possibility(LNM_COMMAND,
                                                      "custom-table",
                                                      args[1]);
                retval = "info: created new log table -- " + args[1];
            }
            else {
                retval = "error: unable to create table -- " + errmsg;
            }
        }
    }

    return retval;
}

static string com_delete_logline_table(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a table name";

    if (args.size() == 0) {
        args.push_back("custom-table");
    }
    else if (args.size() == 2) {
        string rc = lnav_data.ld_vtab_manager->unregister_vtab(args[1]);

        if (rc.empty()) {
            lnav_data.ld_rl_view->rem_possibility(LNM_COMMAND,
                                                  "custom-table",
                                                  args[1]);
            retval = "info: deleted logline table";
        }
        else {
            retval = "error: " + rc;
        }
    }

    return retval;
}

static string com_session(string cmdline, vector<string> &args)
{
    string retval = "error: expecting a command to save to the session file";

    if (args.size() == 0) {}
    else if (args.size() > 2) {
        /* XXX put these in a map */
        if (args[1] != "highlight" &&
            args[1] != "filter-in" &&
            args[1] != "filter-out" &&
            args[1] != "enable-filter" &&
            args[1] != "disable-filter") {
            retval = "error: only the highlight and filter commands are "
                     "supported";
        }
        else if (getenv("HOME") == NULL) {
            retval = "error: the HOME environment variable is not set";
        }
        else {
            string            old_file_name, new_file_name;
            string::size_type space;
            string            saved_cmd;

            space = cmdline.find(' ');
            while (isspace(cmdline[space])) {
                space += 1;
            }
            saved_cmd = cmdline.substr(space);

            old_file_name = dotlnav_path("session");
            new_file_name = dotlnav_path("session.tmp");

            ifstream session_file(old_file_name.c_str());
            ofstream new_session_file(new_file_name.c_str());

            if (!new_session_file) {
                retval = "error: cannot write to session file";
            }
            else {
                bool   added = false;
                string line;

                if (session_file.is_open()) {
                    while (getline(session_file, line)) {
                        if (line == saved_cmd) {
                            added = true;
                            break;
                        }
                        new_session_file << line << endl;
                    }
                }
                if (!added) {
                    new_session_file << saved_cmd << endl;

                    rename(new_file_name.c_str(), old_file_name.c_str());
                }
                else {
                    remove(new_file_name.c_str());
                }

                retval = "info: session file saved";
            }
        }
    }

    return retval;
}

static string com_open(string cmdline, vector<string> &args)
{
    string retval = "error: expecting file name to open";

    if (args.size() == 0) {
        args.push_back("filename");
    }
    else if (args.size() > 1) {
        list<logfile *>::iterator file_iter;
        size_t colon_index;
        int top = 0;
        string fn;

        fn = cmdline.substr(cmdline.find(args[1]));

        if (access(fn.c_str(), R_OK) != 0 &&
            (colon_index = fn.rfind(':')) != string::npos) {
            if (sscanf(&fn.c_str()[colon_index + 1], "%d", &top) == 1 &&
                top >= 0) {
                fn = fn.substr(0, colon_index);
            }
        }

        for (file_iter = lnav_data.ld_files.begin();
             file_iter != lnav_data.ld_files.end();
             ++file_iter) {
            logfile *lf = *file_iter;

            if (lf->get_filename() == fn) {
                if (lf->get_format() != NULL) {
                    retval = "info: log file already loaded";
                    break;
                }
                else {
                    lnav_data.ld_files_to_front.push_back(make_pair(fn, top));
                    retval = "";
                    break;
                }
            }
        }
        if (file_iter == lnav_data.ld_files.end()) {
            auto_mem<char> abspath;
            struct stat    st;

            if (is_glob(fn.c_str())) {
                lnav_data.ld_file_names.insert(make_pair(fn, -1));
                retval = "info: watching -- " + fn;
            }
            else if (stat(fn.c_str(), &st) == -1) {
                retval = "error: cannot stat file: " + fn + " " + strerror(errno);
            }
            else if ((abspath = realpath(fn.c_str(), NULL)) == NULL) {
                retval = "error: cannot find file";
            }
            else {
                fn = abspath.in();
                lnav_data.ld_file_names.insert(make_pair(fn, -1));
                retval = "info: opened -- " + fn;
                lnav_data.ld_files_to_front.push_back(make_pair(fn, top));
            }
        }
    }

    return retval;
}

static string com_close(string cmdline, vector<string> &args)
{
    string retval = "error: close must be run in the log or text file views";

    if (args.empty()) {

    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.top();
        string fn;

        if (tc == &lnav_data.ld_views[LNV_TEXT]) {
            textfile_sub_source &tss = lnav_data.ld_text_source;

            if (tss.tss_files.empty()) {
                retval = "error: no text files are opened";
            }
            else {
                fn = tss.current_file()->get_filename();
                tss.current_file()->close();

                if (tss.tss_files.size() == 1) {
                    lnav_data.ld_view_stack.pop();
                }
            }
        }
        else if (tc == &lnav_data.ld_views[LNV_LOG]) {
            if (tc->get_inner_height() == 0) {
                retval = "error: no log files loaded";
            }
            else {
                logfile_sub_source &lss = lnav_data.ld_log_source;
                vis_line_t vl = tc->get_top();
                content_line_t cl = lss.at(vl);
                logfile *lf = lss.find(cl);

                fn = lf->get_filename();
                lf->close();
            }
        }
        if (!fn.empty()) {
            lnav_data.ld_file_names.erase(make_pair(fn, -1));
            retval = "info: closed -- " + fn;
        }
    }

    return retval;
}

static string com_partition_name(string cmdline, vector<string> &args)
{
    string retval = "error: expecting partition name";

    if (args.size() == 0) {
        args.push_back("enabled-filter");
    }
    else if (args.size() > 1) {
        textview_curses &tc = lnav_data.ld_views[LNV_LOG];
        logfile_sub_source &lss = lnav_data.ld_log_source;
        std::map<content_line_t, bookmark_metadata> &bm = lss.get_user_bookmark_metadata();

        args[1] = cmdline.substr(cmdline.find(args[1]));

        bookmark_vector<vis_line_t> &bv = tc.get_bookmarks()[&textview_curses::BM_USER];
        bookmark_vector<vis_line_t>::iterator iter;

        iter = lower_bound(bv.begin(), bv.end(), vis_line_t(tc.get_top() + 1));
        if (iter == bv.begin()) {
            retval = "error: no marks, press 'm' to mark the start of a partition";
        }
        else {
            --iter;

            bookmark_metadata &line_meta = bm[lss.at(*iter)];

            line_meta.bm_name = args[1];
            retval = "info: name set for partition";
        }
    }

    return retval;
}

static string com_summarize(string cmdline, vector<string> &args)
{
    static pcrecpp::RE db_column_converter("\"");

    string retval = "";

    if (args.size() == 0) {
        args.push_back("colname");
        return retval;
    }
    else if (!setup_logline_table()) {
        retval = "error: no log data available";
    }
    else if (args.size() == 1) {
        retval = "error: no columns specified";
    }
    else {
        auto_mem<char, sqlite3_free> query_frag;
        std::vector<string>          other_columns;
        std::vector<string>          num_columns;
        auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);
        int retcode;
        string query;

        query = "SELECT ";
        for (size_t lpc = 1; lpc < args.size(); lpc++) {
            if (lpc > 1)
                query += ", ";
            query += args[lpc];
        }
        query += " FROM logline ";

        retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                     query.c_str(),
                                     -1,
                                     stmt.out(),
                                     NULL);
        switch (sqlite3_step(stmt.in())) {
            case SQLITE_OK:
            case SQLITE_DONE:
            {
                return "error: no data";
            }
            break;
            case SQLITE_ROW:
            break;
            default:
            {
                const char *errmsg;

                errmsg = sqlite3_errmsg(lnav_data.ld_db);
                return "error: " + string(errmsg);
            }
            break;
        }

        for (int lpc = 0; lpc < sqlite3_column_count(stmt.in()); lpc++) {
            switch (sqlite3_column_type(stmt.in(), lpc)) {
            case SQLITE_INTEGER:
            case SQLITE_FLOAT:
                num_columns.push_back(args[lpc + 1]);
                break;
            default:
                other_columns.push_back(args[lpc + 1]);
                break;
            }
        }

        query = "SELECT";
        for (std::vector<string>::iterator iter = other_columns.begin();
             iter != other_columns.end();
             ++iter) {
            if (iter != other_columns.begin()) {
                query += ",";
            }
            query_frag = sqlite3_mprintf(" %s as \"c_%s\", count(*) as \"count_%s\"",
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str());
            query += query_frag;
        }

        if (!other_columns.empty() && !num_columns.empty()) {
            query += ", ";
        }

        for (std::vector<string>::iterator iter = num_columns.begin();
             iter != num_columns.end();
             ++iter) {
            if (iter != num_columns.begin()) {
                query += ",";
            }
            query_frag = sqlite3_mprintf(" sum(\"%s\"), "
                                         " min(\"%s\"), "
                                         " avg(\"%s\"), "
                                         " median(\"%s\"), "
                                         " stddev(\"%s\"), "
                                         " max(\"%s\") ",
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str(),
                                         iter->c_str());
            query += query_frag;
        }

        query += " FROM logline WHERE startswith(logline.log_part, '.') = 0 ";

        for (std::vector<string>::iterator iter = other_columns.begin();
             iter != other_columns.end();
             ++iter) {
            if (iter == other_columns.begin()) {
                query += " GROUP BY ";
            }
            else{
                query += ",";
            }
            query_frag = sqlite3_mprintf(" \"c_%s\"", iter->c_str());
            query     += query_frag;
        }

        for (std::vector<string>::iterator iter = other_columns.begin();
             iter != other_columns.end();
             ++iter) {
            if (iter == other_columns.begin()) {
                query += " ORDER BY ";
            }
            else{
                query += ",";
            }
            query_frag = sqlite3_mprintf(" \"count_%s\" desc, \"c_%s\" collate naturalnocase asc",
                                         iter->c_str(),
                                         iter->c_str());
            query += query_frag;
        }

        fprintf(stderr, "query %s\n", query.c_str());

        db_label_source &      dls = lnav_data.ld_db_rows;
        hist_source &          hs  = lnav_data.ld_db_source;

        hs.clear();
        dls.clear();
        retcode = sqlite3_prepare_v2(lnav_data.ld_db.in(),
                                     query.c_str(),
                                     -1,
                                     stmt.out(),
                                     NULL);

        if (retcode != SQLITE_OK) {
            const char *errmsg = sqlite3_errmsg(lnav_data.ld_db);

            retval = "error: " + string(errmsg);
        }
        else if (stmt == NULL) {
            retval = "";
        }
        else {
            bool done = false;

            while (!done) {
                retcode = sqlite3_step(stmt.in());

                switch (retcode) {
                case SQLITE_OK:
                case SQLITE_DONE:
                    done = true;
                    break;

                case SQLITE_ROW:
                    sql_callback(stmt.in());
                    break;

                default:
                {
                    const char *errmsg;

                    fprintf(stderr, "code %d\n", retcode);
                    errmsg = sqlite3_errmsg(lnav_data.ld_db);
                    retval = "error: " + string(errmsg);
                    done   = true;
                }
                break;
                }
            }

            if (retcode == SQLITE_DONE) {
                lnav_data.ld_views[LNV_LOG].reload_data();
                lnav_data.ld_views[LNV_DB].reload_data();
                lnav_data.ld_views[LNV_DB].set_left(0);

                if (dls.dls_rows.size() > 0) {
                    ensure_view(&lnav_data.ld_views[LNV_DB]);
                }
            }

            lnav_data.ld_bottom_source.update_loading(0, 0);
            lnav_data.ld_status[LNS_BOTTOM].do_update();
        }
    }

    return retval;
}

static string com_add_test(string cmdline, vector<string> &args)
{
    string retval = "";

    if (args.size() == 0) {}
    else if (args.size() > 1) {
        retval = "error: not expecting any arguments";
    }
    else {
        textview_curses *tc = lnav_data.ld_view_stack.top();

        bookmark_vector<vis_line_t> &bv =
            tc->get_bookmarks()[&textview_curses::BM_USER];
        bookmark_vector<vis_line_t>::iterator iter;

        for (iter = bv.begin(); iter != bv.end(); ++iter) {
            auto_mem<FILE> file(fclose);
            char           path[PATH_MAX];
            string         line;

            tc->grep_value_for_line(*iter, line);

            line.insert(0, 13, ' ');

            snprintf(path, sizeof(path),
                     "%s/test/log-samples/sample-%s.txt",
                     getenv("LNAV_SRC"),
                     hash_string(line).c_str());

            if ((file = fopen(path, "w")) == NULL) {
                perror("fopen failed");
            }
            else {
                fprintf(file, "%s\n", line.c_str());
            }
        }
    }

    return retval;
}

void init_lnav_commands(readline_context::command_map_t &cmd_map)
{
    cmd_map["adjust-log-time"]      = com_adjust_log_time;
    cmd_map["unix-time"]            = com_unix_time;
    cmd_map["current-time"]         = com_current_time;
    cmd_map["goto"]                 = com_goto;
    cmd_map["graph"]                = com_graph;
    cmd_map["help"]                 = com_help;
    cmd_map["highlight"]            = com_highlight;
    cmd_map["filter-in"]            = com_filter;
    cmd_map["filter-out"]           = com_filter;
    cmd_map["append-to"]            = com_save_to;
    cmd_map["write-to"]             = com_save_to;
    cmd_map["write-csv-to"]         = com_save_to;
    cmd_map["enable-filter"]        = com_enable_filter;
    cmd_map["disable-filter"]       = com_disable_filter;
    cmd_map["create-logline-table"] = com_create_logline_table;
    cmd_map["delete-logline-table"] = com_delete_logline_table;
    cmd_map["open"]                 = com_open;
    cmd_map["close"]                = com_close;
    cmd_map["partition-name"]       = com_partition_name;
    cmd_map["session"]              = com_session;
    cmd_map["summarize"]            = com_summarize;

    if (getenv("LNAV_SRC") != NULL) {
        cmd_map["add-test"] = com_add_test;
    }
}
