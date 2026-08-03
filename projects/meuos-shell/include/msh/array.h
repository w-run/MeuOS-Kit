#ifndef MSH_ARRAY_H
#define MSH_ARRAY_H

/* bash 风格数组 API */
int msh_array_set(const char *name, int idx, const char *val);
const char *msh_array_get(const char *name, int idx);
char *msh_array_get_all(const char *name, const char *sep);
int msh_array_count(const char *name);
int msh_is_array(const char *name);
int msh_array_parse_assign(const char *assignment);  /* arr=(a b c) */
int msh_array_parse_indexed(const char *assignment);  /* arr[0]=val */

#endif
