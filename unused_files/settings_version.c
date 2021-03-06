/* Copyright 2012 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "nsmtracker.h"
#include "settings_proc.h"
#include "OS_settings_proc.h"

static const char* get_value(const char* line){
  int pos=0;
  while(line[pos]!='='){
    if(line[pos]==0)
      return NULL;
    pos++;
  }

  pos++;
  if(line[pos]==0)
    return NULL;

  while(isblank(line[pos])){
    if(line[pos]==0)
      return NULL;
    pos++;
  }

  return line+pos;
}

static bool line_has_key(const char* key, const char* string){
  if(isblank(string[0])) // strip whitespace
    return line_has_key(key,string+1);

  if(string[0]=='=' && key[0]==0)
    return true;

  if(key[0]==0)
    return false;

  if(string[0]==0)
    return false;

  if(key[0]==string[0])
    return line_has_key(key+1,string+1);

  return false;
}

static int find_linenum(const char* key, const char** lines){
  int linenum = 0;
  while(lines[linenum]!=NULL){
    if(line_has_key(key,lines[linenum]))
      return linenum;
    linenum++;
  }
  return -1;
}

static const char** get_lines(void){
  FILE *file = fopen(OS_get_config_filename(), "r");

  if(file==NULL){
    const char* curr_dir = OS_get_current_directory();
    const char* separator = OS_get_directory_separator();
    char filename[strlen(curr_dir)+strlen(separator)+strlen("config")+10];
    sprintf(filename,"%s%s%s",curr_dir,separator,"config");
    file = fopen(filename,"r");
  }

  const char** ret = talloc(10000 * sizeof(char*));

  if(file==NULL)
    return ret;

  int linenum = 0;
  char line[500];  
  while(fgets(line,499,file)!=NULL){
    if (strlen(line)>=2){ // Remove line shift if reading file with DOS char set
      size_t dos_pos = strcspn(line, "\r\n"); 
      line[dos_pos] = 0; 
    }
    if(line[strlen(line)-1]=='\n')
      line[strlen(line)-1] = 0;
    ret[linenum] = talloc_strdup(line);
    linenum++;
  }

  fclose(file);

  return ret;
}

static void write_lines(const char** lines){
  const char* filename = OS_get_config_filename();
  FILE *file = fopen(filename, "w");
  if(file==NULL){
    RError("Unable to write config data to \"%s\"",filename);
    return;
  }

  int i = 0;
  while(lines[i]!=NULL)
    fprintf(file,"%s\n",lines[i++]);

  fclose(file);
}

static void append_line(const char** lines, const char* line){
  int i=0;
  while(lines[i]!=NULL)
    i++;
  lines[i] = line;
}

static void SETTINGS_put(const char* key, const char* val){
  const char** lines = get_lines();

  // add key/val
  {
    int linenum = find_linenum(key,lines);

    char* temp = talloc_atomic(strlen(key)+strlen(val)+10);
    sprintf(temp,"%s = %s",key,val);

    if(linenum==-1)
      append_line(lines, temp);
    else
      lines[linenum] = temp;
  }

  // add version
  {
    int linenum = find_linenum("version",lines);

    char* temp = talloc_atomic(strlen(key)+strlen(val)+10);
    sprintf(temp,"%s = " # SETTINGSVERSION , version);

    if(linenum==-1)
      append_line(lines, temp);
    else
      lines[linenum] = temp;
  }


  write_lines(lines);
}

static const char* SETTINGS_get(const char* key){
  const char** lines = get_lines();
  int linenum = find_linenum(key,lines);

  if(linenum==-1)
    return NULL;

  return get_value(lines[linenum]);
}

static bool SETTINGS_is_valid_version(double version){
  if(version==ANY_VERSION)
    return true;
  else{
    const char* val = SETTINGS_get("version");
    if(val==NULL)
      return false;
    else{
      double diskversion=OS_get_double_from_string(val);
      if(version>diskversion)
        return false;
      else
        return true;
    }
  }
}

bool SETTINGS_read_bool(const char* key, bool def, double version){
  return SETTINGS_read_string(key, def==true?"true":"false", version)[0] == 't';
}

int64_t SETTINGS_read_int(const char* key, int64_t def, double version){
  if(SETTINGS_is_valid_version(version)==false)
    return def;

  const char* val = SETTINGS_get(key);

  if(val==NULL)
    return def;
  else
    return atoll(val);
}

double SETTINGS_read_double(const char* key, double def, double version){
  if(SETTINGS_is_valid_version(version)==false)
    return def;

  const char* val = SETTINGS_get(key);

  if(val==NULL)
    return def;
  else
    return OS_get_double_from_string(val);
}

const char* SETTINGS_read_string(const char* key, const char* def, double version){
  if(SETTINGS_is_valid_version(version)==false)
    return def;

  const char* val = SETTINGS_get(key);

  if(val==NULL)
    return def;
  else
    return val;
}

void SETTINGS_write_bool(const char* key, bool val){
  SETTINGS_write_string(key, val==true?"true":"false");
}

void SETTINGS_write_int(const char* key, int64_t val){
  char temp[500];
  sprintf(temp,"%" PRId64,val);
  SETTINGS_put(key,temp);
}

void SETTINGS_write_double(const char* key, double val){
  char temp[500];
  sprintf(temp,"%f",val);
  SETTINGS_put(key,temp);
}

void SETTINGS_write_string(const char* key, const char* val){
  SETTINGS_put(key,val);
}
