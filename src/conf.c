/*
 * This file is part of the zlog Library.
 *
 * Copyright (C) 2011 by Hardy Simpson <HardySimpson1984@gmail.com>
 *
 * Licensed under the LGPL v2.1, see the file COPYING in base directory.
 */

#include "fmacros.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "conf.h"
#include "rule.h"
#include "format.h"
#include "level_list.h"
#include "rotater.h"
#include "zc_defs.h"


/*******************************************************************************/
#ifndef CONSTANTS_DEFAULT
#define CONSTANTS_DEFAULT
#define ZLOG_CONF_DEFAULT_FORMAT "ph_default_format = \"%M(carriage)%T/%M(sysid)(%p) %d(%T) %x %M(levelid):%M(log_block):%M(function)():%M(lineno)> %m\""
#define ZLOG_CONF_DEFAULT_RULE "ph_default_category.*        >stdout"   //<Default rule is for all the log_blocks
#define ZLOG_CONF_STDOUT_RULE "stdout_category.*         >stdout"    //<stdout for all modules need to only output to stdout. like prompt function
#define ZLOG_CONF_COMMAND_RULE "command.*        >stdout"   //< for "command" logblock

#define ZLOG_CONF_DEFAULT_BUF_SIZE_MIN 1024
#define ZLOG_CONF_DEFAULT_BUF_SIZE_MAX (2 * 1024 * 1024)
#define ZLOG_CONF_DEFAULT_FILE_PERMS 0600
#define ZLOG_CONF_DEFAULT_RELOAD_CONF_PERIOD 0
#define ZLOG_CONF_DEFAULT_FSYNC_PERIOD 0
#define ZLOG_CONF_BACKUP_ROTATE_LOCK_FILE "/tmp/zlog.lock"
#endif
/*******************************************************************************/


void zlog_conf_profile(zlog_conf_t * a_conf, int flag)
{
    int i;
    zlog_rule_t *a_rule;
    zlog_format_t *a_format;

    zc_assert(a_conf,);
    zc_profile(flag, "-conf[%p]-", a_conf);
    zc_profile(flag, "--global--");
    zc_profile(flag, "---file[%s],mtime[%s]---", a_conf->file, a_conf->mtime);
    zc_profile(flag, "---strict init[%d]---", a_conf->strict_init);
    zc_profile(flag, "---buffer min[%ld]---", a_conf->buf_size_min);
    zc_profile(flag, "---buffer max[%ld]---", a_conf->buf_size_max);
    if (a_conf->default_format) {
        zc_profile(flag, "---default_format---");
        zlog_format_profile(a_conf->default_format, flag);
    }
    zc_profile(flag, "---file perms[0%o]---", a_conf->file_perms);
    zc_profile(flag, "---reload conf period[%ld]---", a_conf->reload_conf_period);
    zc_profile(flag, "---fsync period[%ld]---", a_conf->fsync_period);

    zc_profile(flag, "---rotate lock file[%s]---", a_conf->rotate_lock_file);
    if (a_conf->rotater) zlog_rotater_profile(a_conf->rotater, flag);

    if (a_conf->levels) zlog_level_list_profile(a_conf->levels, flag);

    if (a_conf->formats) {
        zc_profile(flag, "--format list[%p]--", a_conf->formats);
        zc_arraylist_foreach(a_conf->formats, i, a_format) {
            zlog_format_profile(a_format, flag);
        }
    }

    if (a_conf->rules) {
        zc_profile(flag, "--rule_list[%p]--", a_conf->rules);
        zc_arraylist_foreach(a_conf->rules, i, a_rule) {
            zlog_rule_profile(a_rule, flag);
        }
    }

    return;
}
/*******************************************************************************/
void zlog_conf_del(zlog_conf_t * a_conf)
{
    zc_assert(a_conf,);
    if (a_conf->rotater) zlog_rotater_del(a_conf->rotater);
    if (a_conf->levels) zlog_level_list_del(a_conf->levels);
    if (a_conf->default_format) zlog_format_del(a_conf->default_format);
    if (a_conf->formats) zc_arraylist_del(a_conf->formats);
    if (a_conf->rules) zc_arraylist_del(a_conf->rules);
    free(a_conf);
    zc_debug("zlog_conf_del[%p]");
    return;
}

static int zlog_conf_build_without_file(zlog_conf_t * a_conf);
static int zlog_conf_build_with_file(zlog_conf_t * a_conf);


zlog_conf_t *zlog_conf_new(const char *confpath)
{
    int nwrite = 0;
    int has_conf_file = 0;
    zlog_conf_t *a_conf = NULL;

    a_conf = calloc(1, sizeof(zlog_conf_t));
    if (!a_conf) {
        zc_error("calloc fail, errno[%d]", errno);
        return NULL;
    }

    if (confpath && confpath[0] != '\0') {
        nwrite = snprintf(a_conf->file, sizeof(a_conf->file), "%s", confpath);
        has_conf_file = 1;
    } else if (getenv("ZLOG_CONF_PATH") != NULL) {
        nwrite = snprintf(a_conf->file, sizeof(a_conf->file), "%s", getenv("ZLOG_CONF_PATH"));
        has_conf_file = 1;
    } else {
        memset(a_conf->file, 0x00, sizeof(a_conf->file));
        has_conf_file = 0;
    }
    if (nwrite < 0 || nwrite >= sizeof(a_conf->file)) {
        zc_error("not enough space for path name, nwrite=[%d], errno[%d]", nwrite, errno);
        goto err;
    }

    /* set default configuration start */
    a_conf->strict_init = 1;
    a_conf->buf_size_min = ZLOG_CONF_DEFAULT_BUF_SIZE_MIN;
    a_conf->buf_size_max = ZLOG_CONF_DEFAULT_BUF_SIZE_MAX;
    if (has_conf_file) {
        /* configure file as default lock file */
        strcpy(a_conf->rotate_lock_file, a_conf->file);
    } else {
        strcpy(a_conf->rotate_lock_file, ZLOG_CONF_BACKUP_ROTATE_LOCK_FILE);
    }
    strcpy(a_conf->default_format_line, ZLOG_CONF_DEFAULT_FORMAT);
    a_conf->file_perms = ZLOG_CONF_DEFAULT_FILE_PERMS;
    a_conf->reload_conf_period = ZLOG_CONF_DEFAULT_RELOAD_CONF_PERIOD;
    a_conf->fsync_period = ZLOG_CONF_DEFAULT_FSYNC_PERIOD;
    /* set default configuration end */

    a_conf->levels = zlog_level_list_new();
    if (!a_conf->levels) {
        zc_error("zlog_level_list_new fail");
        goto err;
    }

    a_conf->formats = zc_arraylist_new((zc_arraylist_del_fn) zlog_format_del);
    if (!a_conf->formats) {
        zc_error("zc_arraylist_new fail");
        goto err;
    }

    a_conf->rules = zc_arraylist_new((zc_arraylist_del_fn) zlog_rule_del);
    if (!a_conf->rules) {
        zc_error("init rule_list fail");
        goto err;
    }

    if (has_conf_file) {
        if (zlog_conf_build_with_file(a_conf)) {
            zc_error("zlog_conf_build_with_file fail");
            goto err;
        }
    } else {
        if (zlog_conf_build_without_file(a_conf)) {
            zc_error("zlog_conf_build_without_file fail");
            goto err;
        }
    }

    zlog_conf_profile(a_conf, ZC_DEBUG);
    return a_conf;
    err:
    zlog_conf_del(a_conf);
    return NULL;
}
/*******************************************************************************/
static int zlog_conf_build_without_file(zlog_conf_t * a_conf)
{
    zlog_rule_t *default_rule;

    a_conf->default_format = zlog_format_new(a_conf->default_format_line, &(a_conf->time_cache_count));
    if (!a_conf->default_format) {
        zc_error("zlog_format_new fail");
        return -1;
    }

    a_conf->rotater = zlog_rotater_new(a_conf->rotate_lock_file);
    if (!a_conf->rotater) {
        zc_error("zlog_rotater_new fail");
        return -1;
    }
    default_rule = zlog_rule_new(
            ZLOG_CONF_DEFAULT_RULE,
            a_conf->levels,
            a_conf->default_format,
            a_conf->formats,
            a_conf->file_perms,
            a_conf->fsync_period,
            &(a_conf->time_cache_count));
    if (!default_rule) {
        zc_error("zlog_rule_new fail");
        return -1;
    }

    /* add default rule */
    if (zc_arraylist_add(a_conf->rules, default_rule)) {
        zlog_rule_del(default_rule);
        zc_error("zc_arraylist_add fail");
        return -1;
    }

    return 0;
}
/*******************************************************************************/
static int zlog_conf_parse_line(zlog_conf_t * a_conf, char *line, int *section);


int zlog_ph_conf_build_with_xml(zlog_conf_t * a_conf, xmlNodePtr xml_conf )
{
    int rc = 0;
    int default_rule_init = 0;
    char line[MAXLEN_CFG_LINE + 1];
    int line_no = 0;

    int section = 0;

    char str_temp[] = ZLOG_CONF_DEFAULT_RULE;  //To check if default rule defined by user.
    char str_temp2[] = ZLOG_CONF_COMMAND_RULE;

    const char deli[] = ".";
    char *def_rule_name, *command_rule_name;

    def_rule_name = strtok(str_temp, deli);
    command_rule_name = strtok(str_temp2, deli);

    int def_rule_parsed = 0, command_rule_parsed = 0;

    section = 3;						//Parse default format

    zlog_conf_parse_line(a_conf, ZLOG_CONF_DEFAULT_FORMAT, &section);


    memset(&line, 0x00, sizeof(line));

    xmlChar	*xc = 0;
    xmlNodePtr j=0;
    for(j=xml_conf->children;j;j=j->next) {
        if (j->type==XML_ELEMENT_NODE){

            memset(&line, 0x00, sizeof(line));

            if (strcasecmp((char*)j->name,"Global")==0){section = 1;}
            else if(strcasecmp((char*)j->name,"Level")==0){section = 2;}
            else if(strcasecmp((char*)j->name,"Format")==0){section = 3;}
            else if(strcasecmp((char*)j->name,"Rule")==0){
                section = 4;
                if(!default_rule_init)
                {
                    if (a_conf->reload_conf_period != 0
                        && a_conf->fsync_period >= a_conf->reload_conf_period) {
                        zc_warn("fsync_period[%ld] >= reload_conf_period[%ld],"
                                "set fsync_period to zero");
                        a_conf->fsync_period = 0;
                    }

                    a_conf->rotater = zlog_rotater_new(a_conf->rotate_lock_file);
                    if (!a_conf->rotater) {
                        zc_error("zlog_rotater_new fail");
                        return -1;
                    }

                    a_conf->default_format = zlog_format_new(a_conf->default_format_line,
                                                             &(a_conf->time_cache_count));
                    if (!a_conf->default_format) {
                        zc_error("zlog_format_new fail");
                        return -1;
                    }
                    default_rule_init=1;
                }
            }
            switch(section)
            {

                case 1:

                    xc = xmlGetProp(j,(xmlChar*) "key");

                    memcpy(line+ strlen(line), (char*) xc, strlen((char*) xc));

                    memcpy(line+ strlen(line), " = ", 3);

                    xc = xmlGetProp(j,(xmlChar*) "value");

                    line[strlen(line)]=' ';

                    memcpy(line+ strlen(line), (char*) xc, strlen((char*) xc));


                    break;


                case 2:

                    xc = xmlGetProp(j,(xmlChar*)"level");

                    memcpy(line+ strlen(line), (char*) xc, strlen((char*) xc));

                    memcpy(line+ strlen(line), " = ", 3);

                    xc = xmlGetProp(j,(xmlChar*)"value");

                    line[strlen(line)]=' ';

                    memcpy(line+ strlen(line), (char*) xc, strlen((char*) xc));



                    break;
                case 3:

                    xc = xmlGetProp(j,(xmlChar*)"name");

                    memcpy(line+ strlen(line), (char*) xc, strlen((char*) xc));

                    memcpy(line+ strlen(line), " = ", 3);

                    xc = xmlGetProp(j,(xmlChar*)"pattern");

                    char temp= ' ';

                    if(xc[0] != '"')
                        temp = '"';


                    memcpy(line+ strlen(line), &temp, 1);

                    memcpy(line+ strlen(line), (char*) xc, strlen((char*) xc));

                    memcpy(line+ strlen(line), &temp, 1);

                    break;

                case 4:
                    xc = xmlGetProp(j,(xmlChar*)"category");

                    if ( def_rule_name && strcmp((char*)xc, def_rule_name) == 0)
                        def_rule_parsed = 1;

                    if ( command_rule_name && strcmp((char*)xc, command_rule_name) == 0)
                        command_rule_parsed = 1;

                    memcpy(line + strlen(line), (char*) xc, strlen((char*) xc));

                    line[strlen(line)]='.';

                    xc = xmlGetProp(j,(xmlChar*)"level");

                    memcpy(line + strlen(line), (char*) xc, strlen((char*) xc));

                    line[strlen(line)]=' ';

                    xc = xmlGetProp(j,(xmlChar*)"output");

                    unsigned int i =0 , y = 0, z=0;
                    if(xc[0]=='/' || (xc[0]=='.' && xc[1]=='/'))      //If not begining with double quotation then add them.
                    {    line[strlen(line)]='"';

                        for (i = 0; i < strlen((char *) xc) ; ++i) {            //And close the quotation here
                            if(xc[i]==' ' || xc[i]==',' || i == strlen((char *) xc)-1) {

                                memcpy(line + strlen(line), (char *) xc, i+1);
                                line[strlen(line)]='"';
                                y=i;
                                break;

                            }
                        }
                    }
                    else
                    {
                        memcpy(line + strlen(line), (char*) xc, strlen((char*) xc));
                        z = 1;
                    }
                    if(!z){
                        int new_counter = 0;
                        int i_2=0;
                        for ( ; y < strlen((char*)xc) && i_2!=-1 ; ++y) {
                            if ((xc[y] == '/' || (xc[y] == '.' && xc[y + 1] == '/')) && (xc[y - 1] != '"' && (y + 1 < strlen((char *) xc)) && xc[y + 1] !='"'))  //If not begining with double quotation then add them.
                            {

                                memcpy(line + strlen(line), (char *) xc + i, new_counter);

                                line[strlen(line)] = '"';
                                i_2 = y;
                                for (; i_2 < strlen((char *) xc); ++i_2) {            //And close the quotation here
                                    if (xc[i_2] == ' ' || xc[i] == ',' || ((i_2 == strlen((char *) xc) - 1) && xc[i_2]!='"')) {

                                        memcpy(line + strlen(line), (char *) xc+y, i_2 + 1);

                                        line[strlen(line)] = '"';
                                        i_2=-1;
                                        break;

                                    }
                                    y++;


                                }
                            }
                            new_counter++;
                        }
                    }
                    if(line[strlen(line)-1]!=';')
                        line[strlen(line)]= ';';

                    xc = xmlGetProp(j,(xmlChar*)"format");

                    line[strlen(line)]=' ';

                    memcpy(line + strlen(line), (char*) xc, strlen((char*) xc));

                    break;
            }
            for ( int i=0 ; i< strlen(line); i++)	// replace  inallowed characters.
                if(line[i]=='$')		//In phoenix (xml) %s is reserved
                    line[i]='%';



            //line = &output;

            if(section<5)
                rc = zlog_conf_parse_line(a_conf, line, &section);

            if (rc < 0) {
                zc_error("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                zc_error("line[%s]", line);
                goto exit;
            } else if (rc > 0) {
                zc_warn("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                zc_warn("line[%s]", line);
                zc_warn("as strict init is set to false, ignore and go on");
                rc = 0;
                continue;
            }

        }


    }

    section = 4;

    if(!default_rule_init)      //Check again if default rule parsed
    {

        if (a_conf->reload_conf_period != 0
            && a_conf->fsync_period >= a_conf->reload_conf_period) {
            zc_warn("fsync_period[%ld] >= reload_conf_period[%ld],"
                    "set fsync_period to zero");
            a_conf->fsync_period = 0;
        }

        a_conf->rotater = zlog_rotater_new(a_conf->rotate_lock_file);
        if (!a_conf->rotater) {
            zc_error("zlog_rotater_new fail");
            return -1;
        }

        a_conf->default_format = zlog_format_new(a_conf->default_format_line,
                                                 &(a_conf->time_cache_count));
        if (!a_conf->default_format) {
            zc_error("zlog_format_new fail");
            return -1;
        }
        default_rule_init=1;

    }

    if(!def_rule_parsed)        //<If default config not defined
    {        zlog_conf_parse_line(a_conf, ZLOG_CONF_DEFAULT_RULE, &section);
    }
    if(!command_rule_parsed)        //<If command rule not defined
    {        zlog_conf_parse_line(a_conf, ZLOG_CONF_COMMAND_RULE, &section);
    }
    zlog_conf_parse_line(a_conf, ZLOG_CONF_STDOUT_RULE , &section); //stdout have to be always present.



    return rc;

    exit:
    return rc;
}
int array_len(char *s[]){
    int i=0;
    while(s[i]!=(char *)'\0'){
        i++;

    }
    return i;
}
void add_element(char* s[], const char* c) {
    int len = array_len(s);
    if(len != 0){
        s[len]=(char *)c;
    }
    else{
        s[len]=(char *)c;
    }
}
void remove_element(char* s[]){

    int len=array_len(s);
    s[len-1]=(char *)'\0';

}

char* get_string_by_key(yajl_val yajl_pointer, char* key)
{


    size_t objlen = yajl_pointer->u.object.len;
    for(int j=0 ; j<objlen ; j++) {

        if (strcmp(key, yajl_pointer->u.object.keys[j]) == 0) {

            yajl_val val = yajl_pointer->u.object.values[j];


            if (YAJL_IS_STRING(val)) {

                return YAJL_GET_STRING(val);
                }


        }
    }

    return 0;

}


int zlog_ph_conf_build_with_json(zlog_conf_t * a_conf, yajl_val nodeptr, char** path )
{
    yajl_val pointer_node,object_node=0;
    char* xc = "";

    int rc = 0;
    int default_rule_init = 0;
    char line[MAXLEN_CFG_LINE + 1];
    int line_no = 0;

    int section = 0;

    char str[] = ZLOG_CONF_DEFAULT_RULE;  //To check if default rule defined by user.
    const char deli[] = ".";
    char *def_rule_name;
    def_rule_name = strtok(str, deli);

    int def_rule_parsed = 0;

    section = 3;						//Parse default format

    zlog_conf_parse_line(a_conf, ZLOG_CONF_DEFAULT_FORMAT, &section);


    memset(&line, 0x00, sizeof(line));

    nodeptr = yajl_tree_get(nodeptr, (const char**)path, yajl_t_object );

    if(nodeptr)
    {
        size_t len = nodeptr->u.object.len;

        for(int i=0;i<len;i++){

            const char * key = nodeptr->u.object.keys[ i ];

            if (strcmp(key,"Global") == 0){
                section = 1;

            }
            else  if (strcmp(key,"Level") == 0){
                section = 2;

            }
            else if (strcmp(key,"Format") == 0){
                section = 3;

            }
            else if (strcmp(key,"Rule") == 0){
                section = 4;
                if(!default_rule_init)
                {
                    if (a_conf->reload_conf_period != 0
                        && a_conf->fsync_period >= a_conf->reload_conf_period) {
                        zc_warn("fsync_period[%ld] >= reload_conf_period[%ld],"
                                "set fsync_period to zero");
                        a_conf->fsync_period = 0;
                    }

                    a_conf->rotater = zlog_rotater_new(a_conf->rotate_lock_file);
                    if (!a_conf->rotater) {
                        zc_error("zlog_rotater_new fail");
                        return -1;
                    }

                    a_conf->default_format = zlog_format_new(a_conf->default_format_line,
                                                             &(a_conf->time_cache_count));
                    if (!a_conf->default_format) {
                        zc_error("zlog_format_new fail");
                        return -1;
                    }
                    default_rule_init=1;
                }
            }
            else
            {
                section =-1;
            }
            switch(section)
            {

                case -1:
                    break ;

                case 1:



                    pointer_node = nodeptr->u.object.values[ i ];

                    for (int j =0 ; j < pointer_node->u.array.len ; j++) {

                        memset(&line, 0x00, sizeof(line));

                        object_node = pointer_node->u.array.values[j];

                        if (!get_string_by_key(object_node, "key"))
                            return -1;


                        memcpy(line + strlen(line), xc, strlen(xc));

                        memcpy(line + strlen(line), " = ", 3);


                        //   xc = xmlGetProp(j,(xmlChar*) "value");
                        if (!get_string_by_key(object_node, "value"))
                            return -1;

                        line[strlen(line)] = ' ';

                        memcpy(line + strlen(line), xc, strlen(xc));


                        for (int i = 0; i < strlen(line); i++)    // replace  inallowed characters.
                            if (line[i] == '$')        //In phoenix (xml) %s is reserved
                                line[i] = '%';



                        //line = &output;

                        if (section < 5 && section > 0)
                            rc = zlog_conf_parse_line(a_conf, line, &section);

                        if (rc < 0) {
                            zc_error("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                            zc_error("line[%s]", line);
                            goto exit;
                        } else if (rc > 0) {
                            zc_warn("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                            zc_warn("line[%s]", line);
                            zc_warn("as strict init is set to false, ignore and go on");
                            rc = 0;
                            continue;
                        }
                    }
                    break;


                case 2:



                    pointer_node = nodeptr->u.object.values[ i ];

                    for (int j =0 ; j < pointer_node->u.array.len ; j++)
                    {
                        memset(&line, 0x00, sizeof(line));

                        object_node =   pointer_node->u.array.values[j];


                     xc = get_string_by_key(object_node, "level");
                    if(!xc)
                        return -1;

                    memcpy(line+ strlen(line), xc, strlen(xc));

                    memcpy(line+ strlen(line), " = ", 3);

                    //  xc = xmlGetProp(j,(xmlChar*)"value");
                        xc = get_string_by_key(object_node, "value");
                        if(!xc)
                            return -1;

                        line[strlen(line)]=' ';

                    memcpy(line+ strlen(line), xc, strlen(xc));


                    for ( int i=0 ; i< strlen(line); i++)	// replace  inallowed characters.
                        if(line[i]=='$')		//In phoenix (xml) %s is reserved
                            line[i]='%';



                    //line = &output;

                    if(section<5 && section > 0)
                        rc = zlog_conf_parse_line(a_conf, line, &section);

                    if (rc < 0) {
                        zc_error("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                        zc_error("line[%s]", line);
                        goto exit;
                    } else if (rc > 0) {
                        zc_warn("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                        zc_warn("line[%s]", line);
                        zc_warn("as strict init is set to false, ignore and go on");
                        rc = 0;
                        continue;
                    }
                    }
                    break;

                case 3:



                    pointer_node = nodeptr->u.object.values[ i ];

                    for (int j =0 ; j < pointer_node->u.array.len ; j++)
                    {
                        memset(&line, 0x00, sizeof(line));

                        object_node  =   pointer_node->u.array.values[j];

                     xc = get_string_by_key(object_node, "name");

                     if(!xc)
                           return -1;


                    memcpy(line+ strlen(line), xc, strlen(xc));

                    memcpy(line+ strlen(line), " = ", 3);

                        xc = get_string_by_key(object_node, "pattern");
                        if(!xc)
                            return -1;


                        char temp= ' ';

                    if(xc[0] != '"')
                        temp = '"';



                    memcpy(line+ strlen(line), &temp, 1);

                    memcpy(line+ strlen(line), xc, strlen(xc));

                    memcpy(line+ strlen(line), &temp, 1);

                    for ( int i=0 ; i< strlen(line); i++)	// replace  inallowed characters.
                        if(line[i]=='$')		//In phoenix (xml) %s is reserved
                            line[i]='%';



                    //line = &output;

                    if(section<5 && section > 0)
                        rc = zlog_conf_parse_line(a_conf, line, &section);

                    if (rc < 0) {
                        zc_error("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                        zc_error("line[%s]", line);
                        goto exit;
                    } else if (rc > 0) {
                        zc_warn("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                        zc_warn("line[%s]", line);
                        zc_warn("as strict init is set to false, ignore and go on");
                        rc = 0;
                        continue;
                    }
                    }

                    break;

                case 4:



                    pointer_node = nodeptr->u.object.values[ i ];

                    for (int j =0 ; j < pointer_node->u.array.len ; j++)
                    {
                        memset(&line, 0x00, sizeof(line));

                        object_node =   pointer_node->u.array.values[j];

                        xc = get_string_by_key(object_node, "category");
                    if(!xc)
                        return -1;

                    if ( def_rule_name && strcmp(xc, def_rule_name) == 0)
                        def_rule_parsed = 1;

                    memcpy(line+ strlen(line), xc, strlen(xc));

                    line[strlen(line)]='.';

                    //    xc = xmlGetProp(j,(xmlChar*)"level");
                     xc = get_string_by_key(object_node, "level");
                     if(!xc)
                          return -1;

                        memcpy(line+ strlen(line), xc, strlen(xc));

                    line[strlen(line)]=' ';

                        xc = get_string_by_key(object_node, "output");
                        if(!xc)
                            return -1;



                        unsigned int i =0 , y = 0, z=0;
                    if(xc[0]=='/' || (xc[0]=='.' && xc[1]=='/'))      //If not begining with double quotation then add them.
                    {    line[strlen(line)]='"';

                        for (i = 0; i < strlen((char *) xc) ; ++i) {            //And close the quotation here
                            if(xc[i]==' ' || xc[i]==',' || i == strlen((char *) xc)-1) {

                                memcpy(line + strlen(line), (char *) xc, i+1);
                                line[strlen(line)]='"';
                                y=i;
                                break;

                            }
                        }
                    }
                    else
                    {
                        memcpy(line+ strlen(line), xc, strlen(xc));
                        z = 1;
                    }
                    if(!z){
                        int new_counter = 0;
                        int i_2=0;
                        for ( ; y < strlen((char*)xc) && i_2!=-1 ; ++y) {
                            if ((xc[y] == '/' || (xc[y] == '.' && xc[y + 1] == '/')) && (xc[y - 1] != '"' && (y + 1 < strlen((char *) xc)) && xc[y + 1] !='"'))  //If not begining with double quotation then add them.
                            {

                                memcpy(line + strlen(line), (char *) xc + i, new_counter);

                                line[strlen(line)] = '"';
                                i_2 = y;
                                for (; i_2 < strlen((char *) xc); ++i_2) {            //And close the quotation here
                                    if (xc[i_2] == ' ' || xc[i] == ',' || ((i_2 == strlen((char *) xc) - 1) && xc[i_2]!='"')) {

                                        memcpy(line + strlen(line), (char *) xc+y, i_2 + 1);

                                        line[strlen(line)] = '"';
                                        i_2=-1;
                                        break;

                                    }
                                    y++;


                                }
                            }
                            new_counter++;
                        }
                    }
                    if(line[strlen(line)-1]!=';')
                        line[strlen(line)]= ';';

                        xc = get_string_by_key(object_node, "format");
                        if(!xc)
                            return -1;


                        line[strlen(line)]=' ';

                    memcpy(line + strlen(line), (char*) xc, strlen((char*) xc));


                    for ( int i=0 ; i< strlen(line); i++)	// replace  inallowed characters.
                        if(line[i]=='$')		//In phoenix (xml) %s is reserved
                            line[i]='%';



                    //line = &output;

                    if(section<5 && section > 0)
                        rc = zlog_conf_parse_line(a_conf, line, &section);

                    if (rc < 0) {
                        zc_error("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                        zc_error("line[%s]", line);
                        goto exit;
                    } else if (rc > 0) {
                        zc_warn("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
                        zc_warn("line[%s]", line);
                        zc_warn("as strict init is set to false, ignore and go on");
                        rc = 0;
                        continue;
                    }
                    }
                    break;
            }



        }
    }


    section = 4;

    if(!default_rule_init)      //Check again if default rule parsed
    {

        if (a_conf->reload_conf_period != 0
            && a_conf->fsync_period >= a_conf->reload_conf_period) {
            zc_warn("fsync_period[%ld] >= reload_conf_period[%ld],"
                    "set fsync_period to zero");
            a_conf->fsync_period = 0;
        }

        a_conf->rotater = zlog_rotater_new(a_conf->rotate_lock_file);
        if (!a_conf->rotater) {
            zc_error("zlog_rotater_new fail");
            return -1;
        }

        a_conf->default_format = zlog_format_new(a_conf->default_format_line,
                                                 &(a_conf->time_cache_count));
        if (!a_conf->default_format) {
            zc_error("zlog_format_new fail");
            return -1;
        }
        default_rule_init=1;

    }

    if(!def_rule_parsed)        //If default config not defined
    {        zlog_conf_parse_line(a_conf, ZLOG_CONF_DEFAULT_RULE, &section);
    }
    zlog_conf_parse_line(a_conf, ZLOG_CONF_STDOUT_RULE , &section); //stdout have to be always present.



    return rc;

    exit:
    return rc;
}

static int zlog_conf_build_with_file(zlog_conf_t * a_conf)
{
    int rc = 0;
    struct zlog_stat a_stat;
    struct tm local_time;
    FILE *fp = NULL;

    char line[MAXLEN_CFG_LINE + 1];
    size_t line_len;
    char *pline = NULL;
    char *p = NULL;
    int line_no = 0;
    int i = 0;
    int in_quotation = 0;

    int section = 0;
    /* [global:1] [levels:2] [formats:3] [rules:4] */

    if (lstat(a_conf->file, &a_stat)) {
        zc_error("lstat conf file[%s] fail, errno[%d]", a_conf->file,
                 errno);
        return -1;
    }
    localtime_r(&(a_stat.st_mtime), &local_time);
    strftime(a_conf->mtime, sizeof(a_conf->mtime), "%F %T", &local_time);

    if ((fp = fopen(a_conf->file, "r")) == NULL) {
        zc_error("open configure file[%s] fail", a_conf->file);
        return -1;
    }

    /* Now process the file.
     */
    pline = line;
    memset(&line, 0x00, sizeof(line));
    while (fgets((char *)pline, sizeof(line) - (pline - line), fp) != NULL) {
        ++line_no;
        line_len = strlen(pline);
        if (pline[line_len - 1] == '\n') {
            pline[line_len - 1] = '\0';
        }

        /* check for end-of-section, comments, strip off trailing
         * spaces and newline character.
         */
        p = pline;
        while (*p && isspace((int)*p))
            ++p;
        if (*p == '\0' || *p == '#')
            continue;

        for (i = 0; p[i] != '\0'; ++i) {
            pline[i] = p[i];
        }
        pline[i] = '\0';

        for (p = pline + strlen(pline) - 1; isspace((int)*p); --p)
            /*EMPTY*/;

        if (*p == '\\') {
            if ((p - line) > MAXLEN_CFG_LINE - 30) {
                /* Oops the buffer is full - what now? */
                pline = line;
            } else {
                for (p--; isspace((int)*p); --p)
                    /*EMPTY*/;
                p++;
                *p = 0;
                pline = p;
                continue;
            }
        } else
            pline = line;

        *++p = '\0';

        /* clean the tail comments start from # and not in quotation */
        in_quotation = 0;
        for (p = line; *p != '\0'; p++) {
            if (*p == '"') {
                in_quotation ^= 1;
                continue;
            }

            if (*p == '#' && !in_quotation) {
                *p = '\0';
                break;
            }
        }

        /* we now have the complete line,
         * and are positioned at the first non-whitespace
         * character. So let's process it
         */
        rc = zlog_conf_parse_line(a_conf, line, &section);
        if (rc < 0) {
            zc_error("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
            zc_error("line[%s]", line);
            goto exit;
        } else if (rc > 0) {
            zc_warn("parse configure file[%s]line_no[%ld] fail", a_conf->file, line_no);
            zc_warn("line[%s]", line);
            zc_warn("as strict init is set to false, ignore and go on");
            rc = 0;
            continue;
        }
    }

    exit:
    fclose(fp);
    return rc;
}

/* section [global:1] [levels:2] [formats:3] [rules:4] */
static int zlog_conf_parse_line(zlog_conf_t * a_conf, char *line, int *section)
{

    int nscan;
    int nread;
    char name[MAXLEN_CFG_LINE + 1];
    char word_1[MAXLEN_CFG_LINE + 1];
    char word_2[MAXLEN_CFG_LINE + 1];
    char word_3[MAXLEN_CFG_LINE + 1];
    char value[MAXLEN_CFG_LINE + 1];
    zlog_format_t *a_format = NULL;
    zlog_rule_t *a_rule = NULL;

    if (strlen(line) > MAXLEN_CFG_LINE) {
        zc_error ("line_len[%ld] > MAXLEN_CFG_LINE[%ld], may cause overflow",
                  strlen(line), MAXLEN_CFG_LINE);
        goto error;
    }

    /* get and set outer section flag, so it is a closure? haha */

    if (line[0] == '[') {
        int last_section = *section;
        nscan = sscanf(line, "[ %[^] \t]", name);
        if (STRCMP(name, ==, "global")) {
            *section = 1;
        } else if (STRCMP(name, ==, "levels")) {
            *section = 2;
        } else if (STRCMP(name, ==, "formats")) {
            *section = 3;
        } else if (STRCMP(name, ==, "rules")) {
            *section = 4;
        } else {
            zc_error("wrong section name[%s]", name);
            return -1;
        }
        /* check the sequence of section, must increase */
        if (last_section >= *section) {
            zc_error("wrong sequence of section, must follow global->levels->formats->rules");
            goto error;
        }

        if (*section == 4) {
            if (a_conf->reload_conf_period != 0
                && a_conf->fsync_period >= a_conf->reload_conf_period) {
                /* as all rule will be rebuilt when conf is reload,
                 * so fsync_period > reload_conf_period will never
                 * cause rule to fsync it's file.
                 * fsync_period will be meaningless and down speed,
                 * so make it zero.
                 */
                zc_warn("fsync_period[%ld] >= reload_conf_period[%ld],"
                        "set fsync_period to zero");
                a_conf->fsync_period = 0;
            }

            /* now build rotater and default_format
             * from the unchanging global setting,
             * for zlog_rule_new() */
            a_conf->rotater = zlog_rotater_new(a_conf->rotate_lock_file);
            if (!a_conf->rotater) {
                zc_error("zlog_rotater_new fail");
                goto error;
            }

            a_conf->default_format = zlog_format_new(a_conf->default_format_line,
                                                     &(a_conf->time_cache_count));
            if (!a_conf->default_format) {
                zc_error("zlog_format_new fail");
                goto error;
            }
        }
        return 0;
    }

    /* process detail */
    switch (*section) {
        case 1:
            memset(name, 0x00, sizeof(name));
            memset(value, 0x00, sizeof(value));
            nscan = sscanf(line, " %[^=]= %s ", name, value);
            if (nscan != 2) {
                zc_error("sscanf [%s] fail, name or value is null", line);
                goto error;
            }

            memset(word_1, 0x00, sizeof(word_1));
            memset(word_2, 0x00, sizeof(word_2));
            memset(word_3, 0x00, sizeof(word_3));
            nread = 0;
            nscan = sscanf(name, "%s%n%s%s", word_1, &nread, word_2, word_3);

            if (STRCMP(word_1, ==, "strict") && STRCMP(word_2, ==, "init")) {
                /* if environment variable ZLOG_STRICT_INIT is set
                 * then always make it strict
                 */
                if (STRICMP(value, ==, "false") && !getenv("ZLOG_STRICT_INIT")) {
                    a_conf->strict_init = 0;
                } else {
                    a_conf->strict_init = 1;
                }
            } else if (STRCMP(word_1, ==, "buffer") && STRCMP(word_2, ==, "min")) {
                a_conf->buf_size_min = zc_parse_byte_size(value);
            } else if (STRCMP(word_1, ==, "buffer") && STRCMP(word_2, ==, "max")) {
                a_conf->buf_size_max = zc_parse_byte_size(value);
            } else if (STRCMP(word_1, ==, "file") && STRCMP(word_2, ==, "perms")) {
                sscanf(value, "%o", &(a_conf->file_perms));
            } else if (STRCMP(word_1, ==, "rotate") &&
                       STRCMP(word_2, ==, "lock") && STRCMP(word_3, ==, "file")) {
                /* may overwrite the inner default value, or last value */
                if (STRCMP(value, ==, "self")) {
                    strcpy(a_conf->rotate_lock_file, a_conf->file);
                } else {
                    strcpy(a_conf->rotate_lock_file, value);
                }
            } else if (STRCMP(word_1, ==, "default") && STRCMP(word_2, ==, "format")) {
                /* so the input now is [format = "xxyy"], fit format's style */
                strcpy(a_conf->default_format_line, line + nread);
            } else if (STRCMP(word_1, ==, "reload") &&
                       STRCMP(word_2, ==, "conf") && STRCMP(word_3, ==, "period")) {
                a_conf->reload_conf_period = zc_parse_byte_size(value);
            } else if (STRCMP(word_1, ==, "fsync") && STRCMP(word_2, ==, "period")) {
                a_conf->fsync_period = zc_parse_byte_size(value);
            } else {
                zc_error("name[%s] is not any one of global options", name);
                if (a_conf->strict_init)  goto error;
            }
            break;
        case 2:
            if (zlog_level_list_set(a_conf->levels, line)) {
                zc_error("zlog_level_list_set fail");
                if (a_conf->strict_init)  goto error;
            }
            break;
        case 3:
            a_format = zlog_format_new(line, &(a_conf->time_cache_count));
            if (!a_format) {
                zc_error("zlog_format_new fail [%s]", line);
                if (a_conf->strict_init) goto error;
                else break;
            }
            if (zc_arraylist_add(a_conf->formats, a_format)) {
                zlog_format_del(a_format);
                zc_error("zc_arraylist_add fail");
                goto error;

            }
            break;
        case 4:
            a_rule = zlog_rule_new(line,
                                   a_conf->levels,
                                   a_conf->default_format,
                                   a_conf->formats,
                                   a_conf->file_perms,
                                   a_conf->fsync_period,
                                   &(a_conf->time_cache_count));

            if (!a_rule) {
                zc_error("zlog_rule_new fail [%s]", line);
                if (a_conf->strict_init)             goto error;

                else break;
            }
            if (zc_arraylist_add(a_conf->rules, a_rule)) {
                zlog_rule_del(a_rule);
                zc_error("zc_arraylist_add fail");
                goto error;
            }
            break;
        default:
            zc_error("not in any section");
            goto error;

    }

    return 0;

error:
    zc_error ("error with line %s\n",line);
    return -1;
}
/********************************Phoenix extending functions***********************************************/

zlog_conf_t *zlog_ph_conf(yajl_val conf_obj , char** path)
{


    int has_conf_file = 0;
    zlog_conf_t *a_conf = NULL;

    a_conf = calloc(1, sizeof(zlog_conf_t));
    if (!a_conf) {
        zc_error("calloc fail, errno[%d]", errno);
        return NULL;
    }


    memset(a_conf->file, 0x00, sizeof(a_conf->file));
    has_conf_file = 0;



    /* set default configuration start */
    a_conf->strict_init = 1;
    a_conf->buf_size_min = ZLOG_CONF_DEFAULT_BUF_SIZE_MIN;
    a_conf->buf_size_max = ZLOG_CONF_DEFAULT_BUF_SIZE_MAX;
    if (has_conf_file) {
        /* configure file as default lock file */
        strcpy(a_conf->rotate_lock_file, a_conf->file);
    } else {
        strcpy(a_conf->rotate_lock_file, ZLOG_CONF_BACKUP_ROTATE_LOCK_FILE);
    }
    strcpy(a_conf->default_format_line, ZLOG_CONF_DEFAULT_FORMAT);
    a_conf->file_perms = ZLOG_CONF_DEFAULT_FILE_PERMS;
    a_conf->reload_conf_period = ZLOG_CONF_DEFAULT_RELOAD_CONF_PERIOD;
    a_conf->fsync_period = ZLOG_CONF_DEFAULT_FSYNC_PERIOD;
    /* set default configuration end */

    a_conf->levels = zlog_level_list_new();
    if (!a_conf->levels) {
        zc_error("zlog_level_list_new fail");
        goto err;
    }

    a_conf->formats = zc_arraylist_new((zc_arraylist_del_fn) zlog_format_del);
    if (!a_conf->formats) {
        zc_error("zc_arraylist_new fail");
        goto err;
    }

    a_conf->rules = zc_arraylist_new((zc_arraylist_del_fn) zlog_rule_del);
    if (!a_conf->rules) {
        zc_error("init rule_list fail");
        goto err;
    }

        if (zlog_ph_conf_build_with_json(a_conf, conf_obj, path)) {
            zc_error("zlog_conf_build_without_file fail");
            goto err;
        }


    zlog_conf_profile(a_conf, ZC_DEBUG);
    return a_conf;
    err:
    zlog_conf_del(a_conf);
    return NULL;
}

zlog_conf_t *zlog_ph_conf_xml(xmlNodePtr xml_conf )    //1 json, 0 xml
{
    //xmlNodePtr xml_conf = 0;
   // xml_conf = *(xmlNodePtr*)conf_obj;


    int has_conf_file = 0;
    zlog_conf_t *a_conf = NULL;

    a_conf = calloc(1, sizeof(zlog_conf_t));
    if (!a_conf) {
        zc_error("calloc fail, errno[%d]", errno);
        return NULL;
    }


    memset(a_conf->file, 0x00, sizeof(a_conf->file));
    has_conf_file = 0;



    /* set default configuration start */
    a_conf->strict_init = 1;
    a_conf->buf_size_min = ZLOG_CONF_DEFAULT_BUF_SIZE_MIN;
    a_conf->buf_size_max = ZLOG_CONF_DEFAULT_BUF_SIZE_MAX;
    if (has_conf_file) {
        /* configure file as default lock file */
        strcpy(a_conf->rotate_lock_file, a_conf->file);
    } else {
        strcpy(a_conf->rotate_lock_file, ZLOG_CONF_BACKUP_ROTATE_LOCK_FILE);
    }
    strcpy(a_conf->default_format_line, ZLOG_CONF_DEFAULT_FORMAT);
    a_conf->file_perms = ZLOG_CONF_DEFAULT_FILE_PERMS;
    a_conf->reload_conf_period = ZLOG_CONF_DEFAULT_RELOAD_CONF_PERIOD;
    a_conf->fsync_period = ZLOG_CONF_DEFAULT_FSYNC_PERIOD;
    /* set default configuration end */

    a_conf->levels = zlog_level_list_new();
    if (!a_conf->levels) {
        zc_error("zlog_level_list_new fail");
        goto err;
    }

    a_conf->formats = zc_arraylist_new((zc_arraylist_del_fn) zlog_format_del);
    if (!a_conf->formats) {
        zc_error("zc_arraylist_new fail");
        goto err;
    }

    a_conf->rules = zc_arraylist_new((zc_arraylist_del_fn) zlog_rule_del);
    if (!a_conf->rules) {
        zc_error("init rule_list fail");
        goto err;
    }

        if (zlog_ph_conf_build_with_xml(a_conf, xml_conf)) {
            zc_error("zlog_conf_build_without_file fail");
            goto err;
        }


    zlog_conf_profile(a_conf, ZC_DEBUG);
    return a_conf;
    err:
    zlog_conf_del(a_conf);
    return NULL;
}

