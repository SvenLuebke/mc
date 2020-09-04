dnl
dnl Tabs support
dnl
dnl @author Cosmin Popescu <cosminadrianpopescu@gmail.com>
dnl @copyright Free Software Foundation, Inc.
AC_DEFUN([mc_TABS], [

    PKG_CHECK_MODULES(
    log4c,
    [log4c >= 1.0.0],
    [
        have_check=yes
    ],
    [
        AC_MSG_WARN(['Check' testing framework not found. Check your environment])
    ])
    AC_MSG_CHECKING([for tabs support])
    AC_ARG_WITH(tabs,
            [  --with-tabs          Compile with tabs support @<:@yes@:>@
  --with-tabs=no Do not compile tab support @<:@no@:>@],
            [
                result=no
                if test x$withval = xyes; then
                    AC_DEFINE(WITH_TABS, 1,  [Define to make tabs support optional])
                    result="yes"
                fi
                if test x$withval = xyes; then
                    result="yes"
                fi
            ],
            [
                dnl Default: enable the tabs support
                result="no"
            ])

    AC_MSG_RESULT([$result])
    tabs="$result"

])
