/*
 * lwan - simple web server
 * Copyright (c) 2013 Leandro A. F. Pereira <leandro@hardinfo.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lwan-private.h"

#include "lwan-config.h"
#include "lwan-status.h"
#include "hash.h"

unsigned int parse_time_period(const char *str, unsigned int default_value)
{
    unsigned int total = 0;
    unsigned int period;
    char multiplier;

    if (!str)
        return default_value;

    while (*str && sscanf(str, "%u%c", &period, &multiplier) == 2) {
        switch (multiplier) {
        case 's': total += period; break;
        case 'm': total += period * ONE_MINUTE; break;
        case 'h': total += period * ONE_HOUR; break;
        case 'd': total += period * ONE_DAY; break;
        case 'w': total += period * ONE_WEEK; break;
        case 'M': total += period * ONE_MONTH; break;
        case 'y': total += period * ONE_YEAR; break;
        default:
            lwan_status_warning("Ignoring unknown multiplier: %c",
                        multiplier);
        }

        str = (const char *)rawmemchr(str, multiplier) + 1;
    }

    return total ? total : default_value;
}

long parse_long(const char *value, long default_value)
{
    char *endptr;
    long parsed;

    errno = 0;
    parsed = strtol(value, &endptr, 0);

    if (errno != 0)
        return default_value;

    if (*endptr != '\0' || value == endptr)
        return default_value;

    return parsed;
}

int parse_int(const char *value, int default_value)
{
    long long_value = parse_long(value, default_value);

    if ((long)(int)long_value != long_value)
        return default_value;

    return (int)long_value;
}

bool parse_bool(const char *value, bool default_value)
{
    if (!value)
        return default_value;

    if (streq(value, "true") || streq(value, "on") || streq(value, "yes"))
        return true;

    if (streq(value, "false") || streq(value, "off") || streq(value, "no"))
        return false;

    return parse_int(value, default_value);
}

bool config_error(struct config *conf, const char *fmt, ...)
{
    va_list values;
    int len;
    char *output;

    if (conf->error_message)
        return false;

    va_start(values, fmt);
    len = vasprintf(&output, fmt, values);
    va_end(values);

    if (len >= 0) {
        conf->error_message = output;
        return true;
    }

    conf->error_message = NULL;
    return false;
}

static char *remove_comments(char *line)
{
    char *tmp = strrchr(line, '#');
    if (tmp)
        *tmp = '\0';
    return line;
}

static char *remove_trailing_spaces(char *line)
{
    char *end = rawmemchr(line, '\0');

    for (end--; end >= line && isspace(*end); end--);
    *(end + 1) = '\0';

    return line;
}

static char *remove_leading_spaces(char *line)
{
    while (isspace(*line))
        line++;
    return line;
}

static char *find_line_end(char *line)
{
    if (*line == '\0')
        return line;
    return (char *)rawmemchr(line, '\0') - 1;
}

static bool parse_section(char *line, struct config_line *l, char *bracket)
{
    char *name, *param;
    char *space = strchr(line, ' ');
    if (!space)
        return false;

    *bracket = '\0';
    *space = '\0';
    name = remove_trailing_spaces(remove_leading_spaces(line));
    param = remove_trailing_spaces(remove_leading_spaces(space + 1));

    l->name = name;
    l->param = param;
    l->type = CONFIG_LINE_TYPE_SECTION;

    return true;
}

static char *replace_space_with_underscore(char *line)
{
    for (char *ptr = line; *ptr; ptr++) {
        if (*ptr == ' ')
            *ptr = '_';
    }
    return line;
}

static bool config_fgets(struct config *conf, char *buffer, size_t buffer_len)
{
    assert(buffer_len <= INT_MAX);
    if (!fgets(buffer, (int)buffer_len, conf->file))
        return false;

    if (conf->isolated.end > 0) {
        long curpos = ftell(conf->file);
        if (curpos < 0) {
            config_error(conf, "Could not obtain file position");
            return false;
        }
        if (curpos >= conf->isolated.end)
            return false;
    }
    conf->line++;

    return true;
}

static bool parse_multiline(struct config *c, struct config_line *l)
{
    char buffer[1024];

    if (!strbuf_reset_length(c->strbuf)) {
        config_error(c, "Could not allocate multiline string");
        return false;
    }

    while (config_fgets(c, buffer, sizeof(buffer))) {
        char *tmp = remove_trailing_spaces(remove_leading_spaces(buffer));
        if (streq(tmp, "'''")) {
            l->value = strbuf_get_buffer(c->strbuf);
            return true;
        }

        if (!strbuf_append_str(c->strbuf, buffer, 0)) {
            config_error(c, "Could not append to multiline string");
            return false;
        }
        if (!strbuf_append_char(c->strbuf, '\n')) {
            config_error(c, "Could not append to multiline string");
            return false;
        }
    }

    config_error(c, "EOF while scanning for end of multiline string");
    return false;
}

static bool parse_line(struct config *c, char *line, struct config_line *l, char *equal)
{
    *equal = '\0';
    line = remove_leading_spaces(line);
    line = remove_trailing_spaces(line);

    l->key = replace_space_with_underscore(line);
    l->value = remove_leading_spaces(equal + 1);
    l->type = CONFIG_LINE_TYPE_LINE;

    if (!streq(l->value, "'''"))
        return true;

    return parse_multiline(c, l);
}

static bool find_section_end(struct config *config, struct config_line *line, int recursion_level)
{
    if (recursion_level > 10) {
        config_error(config, "Recursion level too deep");
        return false;
    }

    while (config_read_line(config, line)) {
        switch (line->type) {
        case CONFIG_LINE_TYPE_LINE:
            continue;
        case CONFIG_LINE_TYPE_SECTION:
            if (!find_section_end(config, line, recursion_level + 1))
                return false;
            break;
        case CONFIG_LINE_TYPE_SECTION_END:
            return true;
        }
    }

    return false;
}

bool config_skip_section(struct config *conf, struct config_line *line)
{
    if (conf->error_message)
        return false;
    if (line->type != CONFIG_LINE_TYPE_SECTION)
        return false;
    return find_section_end(conf, line, 0);
}

bool config_isolate_section(struct config *current_conf,
    struct config_line *current_line, struct config *isolated)
{
    long startpos, endpos;
    bool r = false;
    int origin_line = current_conf->line;

    *isolated = *current_conf;
    isolated->file = NULL;

    if (current_conf->error_message)
        return false;
    if (current_line->type != CONFIG_LINE_TYPE_SECTION)
        return false;

    startpos = ftell(current_conf->file);
    if (startpos < 0)
        return false;
    if (!find_section_end(current_conf, current_line, 0))
        goto resetpos;
    current_conf->line = origin_line;
    endpos = ftell(current_conf->file);
    if (endpos < 0)
        goto resetpos;

    if (!config_open(isolated, current_conf->path))
        goto resetpos;
    if (fseek(isolated->file, startpos, SEEK_SET) < 0)
        goto resetpos;

    isolated->isolated.end = endpos;
    r = true;

resetpos:
    if (fseek(current_conf->file, startpos, SEEK_SET) < 0) {
        config_error(current_conf, "Could not reset file position");
        r = false;
    }
    if (!r || current_conf->error_message) {
        config_error(current_conf, "Unknown error while isolating section");
        r = false;
    }
    if (!r)
        config_close(isolated);
    return r;
}

bool config_read_line(struct config *conf, struct config_line *l)
{
    char *line, *line_end;

    if (conf->error_message)
        return false;

    do {
        if (!config_fgets(conf, l->buffer, sizeof(l->buffer)))
            return false;

        line = remove_comments(l->buffer);
        line = remove_leading_spaces(line);
        line = remove_trailing_spaces(line);
    } while (*line == '\0');

    line_end = find_line_end(line);

    if (*line_end == '{') {
        if (!parse_section(line, l, line_end)) {
            config_error(conf, "Malformed section opening");
            return false;
        }
    } else if (*line == '}' && line == line_end) {
        l->type = CONFIG_LINE_TYPE_SECTION_END;
    } else {
        char *equal = strchr(line, '=');
        if (equal) {
            if (!parse_line(conf, line, l, equal)) {
                config_error(conf, "Malformed key=value line");
                return false;
            }
        } else {
            config_error(conf, "Expecting section or key=value");
            return false;
        }
    }

    return true;
}

bool config_open(struct config *conf, const char *path)
{
    if (!conf)
        return false;
    if (!path)
        goto error_no_strbuf;

    conf->strbuf = strbuf_new();
    if (!conf->strbuf)
        goto error_no_strbuf;

    conf->file = fopen(path, "re");
    if (!conf->file)
        goto error_no_file;

    conf->path = strdup(path);
    if (!conf->path)
        goto error_no_path;

    conf->isolated.end = -1;
    conf->line = 0;
    conf->error_message = NULL;

    return true;

error_no_path:
    fclose(conf->file);
error_no_file:
    strbuf_free(conf->strbuf);
error_no_strbuf:
    conf->file = NULL;
    return false;
}

void config_close(struct config *conf)
{
    if (!conf)
        return;
    if (!conf->file)
        return;
    fclose(conf->file);
    free(conf->path);
    free(conf->error_message);
    strbuf_free(conf->strbuf);
}