# Generated by devtools/yamaker.

LIBRARY()

LICENSE(GPL-3.0-or-later)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

ADDINCL(
    contrib/tools/bison
    contrib/tools/bison/lib
)

NO_COMPILER_WARNINGS()

NO_RUNTIME()

CFLAGS(
    -DEXEEXT=\"\"
)

SRCS(
    allocator.c
    areadlink.c
    argmatch.c
    asnprintf.c
    basename-lgpl.c
    basename.c
    binary-io.c
    bitrotate.c
    bitset.c
    bitset/array.c
    bitset/list.c
    bitset/stats.c
    bitset/table.c
    bitset/vector.c
    bitsetv.c
    c-ctype.c
    c-strcasecmp.c
    c-strncasecmp.c
    careadlinkat.c
    cloexec.c
    close-stream.c
    closeout.c
    concat-filename.c
    dirname-lgpl.c
    dirname.c
    dup-safer-flag.c
    dup-safer.c
    exitfail.c
    fatal-signal.c
    fd-safer-flag.c
    fd-safer.c
    fopen-safer.c
    fseterr.c
    get-errno.c
    gethrxtime.c
    getprogname.c
    gl_array_list.c
    gl_list.c
    gl_xlist.c
    hard-locale.c
    hash.c
    localcharset.c
    math.c
    mbrtowc.c
    mbswidth.c
    path-join.c
    pipe-safer.c
    pipe2-safer.c
    pipe2.c
    printf-args.c
    printf-frexp.c
    printf-frexpl.c
    printf-parse.c
    progname.c
    quotearg.c
    sig-handler.c
    spawn-pipe.c
    stripslash.c
    timespec.c
    timevar.c
    unistd.c
    uniwidth/width.c
    vasnprintf.c
    wait-process.c
    wctype-h.c
    xalloc-die.c
    xconcat-filename.c
    xmalloc.c
    xmemdup0.c
    xreadlink.c
    xsize.c
    xstrndup.c
    xtime.c
)

IF (OS_DARWIN)
    SRCS(
        error.c
        fpending.c
        obstack.c
        obstack_printf.c
        strverscmp.c
    )
ELSEIF (OS_WINDOWS)
    ADDINCL(
        GLOBAL contrib/tools/bison/lib/platform/win64
    )
    SRCS(
        close.c
        dup2.c
        error.c
        fcntl.c
        fd-hook.c
        fpending.c
        getdtablesize.c
        getopt.c
        getopt1.c
        msvc-inval.c
        msvc-nothrow.c
        obstack.c
        obstack_printf.c
        open.c
        raise.c
        sigaction.c
        sigprocmask.c
        stpcpy.c
        strndup.c
        strverscmp.c
        waitpid.c
        wcwidth.c
    )
ENDIF()

END()
