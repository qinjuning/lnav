/**
 * Copyright (c) 2013, Timothy Stack
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
 * @file sql_util.cc
 */

#include "config.h"

#include <stdio.h>

#include <pcrecpp.h>

#include "auto_mem.hh"
#include "sql_util.hh"

/**
 * Copied from -- http://www.sqlite.org/lang_keywords.html
 */
const char *sql_keywords[] = {
    "ABORT",
    "ACTION",
    "ADD",
    "AFTER",
    "ALL",
    "ALTER",
    "ANALYZE",
    "AND",
    "AS",
    "ASC",
    "ATTACH",
    "AUTOINCREMENT",
    "BEFORE",
    "BEGIN",
    "BETWEEN",
    "BY",
    "CASCADE",
    "CASE",
    "CAST",
    "CHECK",
    "COLLATE",
    "COLUMN",
    "COMMIT",
    "CONFLICT",
    "CONSTRAINT",
    "CREATE",
    "CROSS",
    "CURRENT_DATE",
    "CURRENT_TIME",
    "CURRENT_TIMESTAMP",
    "DATABASE",
    "DEFAULT",
    "DEFERRABLE",
    "DEFERRED",
    "DELETE",
    "DESC",
    "DETACH",
    "DISTINCT",
    "DROP",
    "EACH",
    "ELSE",
    "END",
    "ESCAPE",
    "EXCEPT",
    "EXCLUSIVE",
    "EXISTS",
    "EXPLAIN",
    "FAIL",
    "FOR",
    "FOREIGN",
    "FROM",
    "FULL",
    "GLOB",
    "GROUP",
    "HAVING",
    "IF",
    "IGNORE",
    "IMMEDIATE",
    "IN",
    "INDEX",
    "INDEXED",
    "INITIALLY",
    "INNER",
    "INSERT",
    "INSTEAD",
    "INTERSECT",
    "INTO",
    "IS",
    "ISNULL",
    "JOIN",
    "KEY",
    "LEFT",
    "LIKE",
    "LIMIT",
    "MATCH",
    "NATURAL",
    "NO",
    "NOT",
    "NOTNULL",
    "NULL",
    "OF",
    "OFFSET",
    "ON",
    "OR",
    "ORDER",
    "OUTER",
    "PLAN",
    "PRAGMA",
    "PRIMARY",
    "QUERY",
    "RAISE",
    "REFERENCES",
    "REGEXP",
    "REINDEX",
    "RELEASE",
    "RENAME",
    "REPLACE",
    "RESTRICT",
    "RIGHT",
    "ROLLBACK",
    "ROW",
    "SAVEPOINT",
    "SELECT",
    "SET",
    "TABLE",
    "TEMP",
    "TEMPORARY",
    "THEN",
    "TO",
    "TRANSACTION",
    "TRIGGER",
    "UNION",
    "UNIQUE",
    "UPDATE",
    "USING",
    "VACUUM",
    "VALUES",
    "VIEW",
    "VIRTUAL",
    "WHEN",
    "WHERE",

    NULL
};

const char *sql_function_names[] = {
    /* http://www.sqlite.org/lang_aggfunc.html */
    "avg",
    "count",
    "group_concat",
    "max",
    "min",
    "sum",
    "total",

    /* http://www.sqlite.org/lang_corefunc.html */
    "abs",
    "changes",
    "char",
    "coalesce",
    "glob",
    "ifnull",
    "instr",
    "hex",
    "last_insert_rowid",
    "length",
    "like",
    "load_extension",
    "lower",
    "ltrim",
    "nullif",
    "quote",
    "random",
    "randomblob",
    "replace",
    "round",
    "rtrim",
    "soundex",
    "sqlite_compileoption_get",
    "sqlite_compileoption_used",
    "sqlite_source_id",
    "sqlite_version",
    "substr",
    "total_changes",
    "trim",
    "typeof",
    "unicode",
    "upper",
    "zeroblob",

    NULL
};

static int handle_db_list(void *ptr,
                          int ncols,
                          char **colvalues,
                          char **colnames)
{
    struct sqlite_metadata_callbacks *smc;

    smc = (struct sqlite_metadata_callbacks *)ptr;

    smc->smc_db_list[colvalues[1]] = std::vector<std::string>();

    return smc->smc_database_list(ptr, ncols, colvalues, colnames);
}

struct table_list_data {
    struct sqlite_metadata_callbacks *tld_callbacks;
    db_table_map_t::iterator *        tld_iter;
};

static int handle_table_list(void *ptr,
                             int ncols,
                             char **colvalues,
                             char **colnames)
{
    struct table_list_data *tld = (struct table_list_data *)ptr;

    (*tld->tld_iter)->second.push_back(colvalues[0]);

    return tld->tld_callbacks->smc_table_list(tld->tld_callbacks,
                                              ncols,
                                              colvalues,
                                              colnames);
}

int walk_sqlite_metadata(sqlite3 *db, struct sqlite_metadata_callbacks &smc)
{
    auto_mem<char, sqlite3_free> errmsg;
    int retval;

    retval = sqlite3_exec(db,
                          "pragma collation_list",
                          smc.smc_collation_list,
                          &smc,
                          errmsg.out());
    if (retval != SQLITE_OK) {
        fprintf(stderr,
                "error: could not get collation list -- %s\n",
                errmsg.in());
        return retval;
    }

    retval = sqlite3_exec(db,
                          "pragma database_list",
                          handle_db_list,
                          &smc,
                          errmsg.out());
    if (retval != SQLITE_OK) {
        fprintf(stderr, "error: could not get DB list -- %s\n", errmsg.in());
        return retval;
    }

    for (db_table_map_t::iterator iter = smc.smc_db_list.begin();
         iter != smc.smc_db_list.end();
         ++iter) {
        struct table_list_data       tld = { &smc, &iter };
        auto_mem<char, sqlite3_free> query;

        query = sqlite3_mprintf("SELECT name FROM %Q.sqlite_master "
                                "WHERE type='table'",
                                iter->first.c_str());

        retval = sqlite3_exec(db,
                              query,
                              handle_table_list,
                              &tld,
                              errmsg.out());
        if (retval != SQLITE_OK) {
            fprintf(stderr,
                    "error: could not get table list -- %s\n",
                    errmsg.in());
            return retval;
        }

        for (db_table_list_t::iterator table_iter = iter->second.begin();
             table_iter != iter->second.end();
             ++table_iter) {
            auto_mem<char, sqlite3_free> table_query;
            std::string &table_name = *table_iter;

            table_query = sqlite3_mprintf(
                "pragma %Q.table_info(%Q)",
                iter->first.c_str(),
                table_name.c_str());
            if (table_query == NULL) {
                return SQLITE_NOMEM;
            }

            retval = sqlite3_exec(db,
                                  table_query,
                                  smc.smc_table_info,
                                  &table_name,
                                  errmsg.out());
            if (retval != SQLITE_OK) {
                fprintf(stderr,
                        "error: could not get table info -- %s\n",
                        errmsg.in());
                return retval;
            }

            table_query = sqlite3_mprintf(
                "pragma %Q.foreign_key_list(%Q)",
                iter->first.c_str(),
                table_name.c_str());
            if (table_query == NULL) {
                return SQLITE_NOMEM;
            }

            retval = sqlite3_exec(db,
                                  table_query,
                                  smc.smc_foreign_key_list,
                                  &table_name,
                                  errmsg.out());
            if (retval != SQLITE_OK) {
                fprintf(stderr,
                        "error: could not get foreign key list -- %s\n",
                        errmsg.in());
                return retval;
            }
        }
    }

    return retval;
}

void attach_sqlite_db(sqlite3 *db, const std::string &filename)
{
    static pcrecpp::RE db_name_converter("[^\\w]");

    auto_mem<sqlite3_stmt> stmt(sqlite3_finalize);

    if (sqlite3_prepare_v2(db,
                           "ATTACH DATABASE ? as ?",
                           -1,
                           stmt.out(),
                           NULL) != SQLITE_OK) {
        fprintf(stderr,
                "error: could not prepare DB attach statement -- %s\n",
                sqlite3_errmsg(db));
        return;
    }

    if (sqlite3_bind_text(stmt.in(), 1,
                          filename.c_str(), filename.length(),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        fprintf(stderr,
                "error: could not bind DB attach statement -- %s\n",
                sqlite3_errmsg(db));
        return;
    }

    size_t base_start = filename.find_last_of("/\\");
    string db_name;

    if (base_start == string::npos) {
        db_name = filename;
    }
    else {
        db_name = filename.substr(base_start + 1);
    }

    db_name_converter.GlobalReplace("_", &db_name);

    if (sqlite3_bind_text(stmt.in(), 2,
                          db_name.c_str(), db_name.length(),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        fprintf(stderr,
                "error: could not bind DB attach statement -- %s\n",
                sqlite3_errmsg(db));
        return;
    }

    if (sqlite3_step(stmt.in()) != SQLITE_DONE) {
        fprintf(stderr,
                "error: could not execute DB attach statement -- %s\n",
                sqlite3_errmsg(db));
        return;
    }
}

void sql_strftime(char *buffer, size_t buffer_size, time_t time, int millis)
{
    struct tm gmtm;

    gmtime_r(&time, &gmtm);
    snprintf(buffer, buffer_size,
             "% 4d-%02d-%02dT%02d:%02d:%02d.%03d",
             gmtm.tm_year + 1900,
             gmtm.tm_mon + 1,
             gmtm.tm_mday,
             gmtm.tm_hour,
             gmtm.tm_min,
             gmtm.tm_sec,
             millis);
}
