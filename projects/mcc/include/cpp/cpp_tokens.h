/* cpp_tokens.h — m++ (C++) lexer tokens.
 *
 * The C++ lexer extends the C token set (tokens.h) with C++ keywords and
 * punctuators.  Token kinds are namespaced CPP_* to coexist with the C
 * tokens during the migration.  Stage C.1.2: keyword table + punctuator
 * extensions; raw strings / user-defined literals / digit separators are
 * added incrementally.
 */
#ifndef MCC_CPP_TOKENS_H
#define MCC_CPP_TOKENS_H

/* C++ keywords (beyond the C set).  These map to distinct token kinds so
 * the C++ parser can dispatch on them. */
enum cpp_tokenkind {
	CPP_TNONE = 0,

	/* C++98/03 core keywords */
	CPP_TCLASS,
	CPP_TSTRUCT,          /* C++ struct (may alias C TSTRUCT) */
	CPP_TUNION,           /* C++ union */
	CPP_TENUM,            /* C++ enum */
	CPP_TNAMESPACE,
	CPP_TUSING,
	CPP_TTEMPLATE,
	CPP_TTYPENAME,
	CPP_TTHIS,
	CPP_TNEW,
	CPP_TDELETE,
	CPP_TPUBLIC,
	CPP_TPRIVATE,
	CPP_TPROTECTED,
	CPP_TFRIEND,
	CPP_TVIRTUAL,
	CPP_TOVERRIDE,
	CPP_TFINAL,
	CPP_TEXPLICIT,
	CPP_TINLINE,           /* C++ inline */
	CPP_TMUTABLE,
	CPP_TCONST_CAST,
	CPP_TSTATIC_CAST,
	CPP_TDYNAMIC_CAST,
	CPP_TREINTERPRET_CAST,
	CPP_TTRUE,
	CPP_TFALSE,
	CPP_TNULLPTR,
	CPP_TOPERATOR,
	CPP_TTHROW,
	CPP_TTRY,
	CPP_TCATCH,
	CPP_TWHILE,            /* C++ while */
	CPP_TDO,               /* C++ do */
	CPP_TFOR,              /* C++ for */
	CPP_TIF,               /* C++ if */
	CPP_TELSE,             /* C++ else */
	CPP_TSWITCH,           /* C++ switch */
	CPP_TCASE,             /* C++ case */
	CPP_TDEFAULT,          /* C++ default */
	CPP_TBREAK,            /* C++ break */
	CPP_TCONTINUE,         /* C++ continue */
	CPP_TRETURN,           /* C++ return */
	CPP_TGOTO,             /* C++ goto */
	CPP_TSTATIC,           /* C++ static */
	CPP_TEXTERN,           /* C++ extern */
	CPP_TTYPEDEF,          /* C++ typedef */
	CPP_TREGISTER,         /* C++ register */
	CPP_TVOLATILE,         /* C++ volatile */
	CPP_TCONST,            /* C++ const */
	CPP_TSIGNED,           /* C++ signed */
	CPP_TUNSIGNED,         /* C++ unsigned */
	CPP_TSHORT,            /* C++ short */
	CPP_TLONG,             /* C++ long */
	CPP_TCHAR,             /* C++ char */
	CPP_TBOOL,             /* C++ bool */
	CPP_TINT,              /* C++ int */
	CPP_TFLOAT,            /* C++ float */
	CPP_TDOUBLE,           /* C++ double */
	CPP_TVOID,             /* C++ void */
	CPP_TAUTO,             /* C++ auto (also C11 storage) */
	CPP_TDECLTYPE,         /* C++11 decltype */
	CPP_TALIGNOF,          /* C++11 alignof */
	CPP_TALIGNAS,          /* C++11 alignas */
	CPP_TTHREAD_LOCAL,     /* C++11 thread_local */
	CPP_TSTATIC_ASSERT,    /* C++11 static_assert */
	CPP_TCONSTEXPR,        /* C++11 constexpr */
	CPP_TCONSTEVAL,        /* C++20 consteval */
	CPP_TNOEXCEPT,         /* C++11 noexcept */
	CPP_TNULLPTR_T,        /* C++11 nullptr_t */

	/* C++20/23 concepts/modules */
	CPP_TCONCEPT,
	CPP_TREQUIRES,
	CPP_TMODULE,
	CPP_TIMPORT,
	CPP_TEXPORT,
	CPP_TCO_AWAIT,
	CPP_TCO_YIELD,
	CPP_TCO_RETURN,

	/* punctuators unique to C++ */
	CPP_TSCOPE,        /* :: (scoping; may reuse C TCOLONCOLON) */
	CPP_TARROW_STAR,   /* ->* */
	CPP_TDOT_STAR,     /* .* */
	CPP_TLEFT_SHIFT_ASSIGN,  /* <<= */
	CPP_TRIGHT_SHIFT_ASSIGN, /* >>= */
	CPP_TELLIPSIS_NEW, /* ... in templates */
	CPP_TSPACESHIP,    /* <=> (C++20) */

	CPP_TEOF,
};

#endif /* MCC_CPP_TOKENS_H */
