
#include "log4c.h"


extern log4c_category_t* log4ccat;

#if 0
  extern int init_examples_lib(void);
  extern int init_userloc_formatters(void); 

  typedef struct {
      const char* hostname;
      int   pid;
  } user_locinfo_t; 


#define log4c(a_category, a_priority, a_format, args...) \
  helper(a_category, __FILE__, __LINE__, __FUNCTION__, &userloc, a_priority, a_format , ## args );

LOG4C_ATTRIBUTE((format(printf, 7, 8)))
static inline void helper(
    const log4c_category_t* a_category,
    const char* file,
    int   line,
    const char* func,
    void* user_locinfo,
    int a_priority,
    const char* a_format,
    ...)
{
  va_list va;
  log4c_location_info_t locinfo;
  locinfo.loc_file = file;
  locinfo.loc_line = line;
  locinfo.loc_function = func;
  locinfo.loc_data = user_locinfo;


  va_start(va, a_format);

  log4c_category_log_locinfo(a_category, &locinfo, a_priority, a_format,va);
       va_end(va);

}

#else

#define log4c(a_category, a_priority, a_format, args...) \
  log4c_category_log(a_category, a_priority, "%-8s:%-20s : %05d : " a_format ,__FILE__, __FUNCTION__, __LINE__, ## args );

#endif

