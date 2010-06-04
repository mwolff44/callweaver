/* A Bison parser, made by GNU Bison 2.3.  */

/* Skeleton implementation for Bison's Yacc-like parsers in C

   Copyright (C) 1984, 1989, 1990, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output.  */
#define YYBISON 1

/* Bison version.  */
#define YYBISON_VERSION "2.3"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 1

/* Using locations.  */
#define YYLSP_NEEDED 1

/* Substitute the variable and function names.  */
#define yyparse cw_yyparse
#define yylex   cw_yylex
#define yyerror cw_yyerror
#define yylval  cw_yylval
#define yychar  cw_yychar
#define yydebug cw_yydebug
#define yynerrs cw_yynerrs
#define yylloc cw_yylloc

/* Tokens.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
   /* Put the tokens into the symbol table, so that GDB and other debuggers
      know about them.  */
   enum yytokentype {
     TOK_COMMA = 258,
     TOK_COLONCOLON = 259,
     TOK_COND = 260,
     TOK_OR = 261,
     TOK_AND = 262,
     TOK_NE = 263,
     TOK_LE = 264,
     TOK_GE = 265,
     TOK_LT = 266,
     TOK_GT = 267,
     TOK_EQ = 268,
     TOK_MINUS = 269,
     TOK_PLUS = 270,
     TOK_MOD = 271,
     TOK_DIV = 272,
     TOK_MULT = 273,
     TOK_COMPL = 274,
     TOK_EQTILDE = 275,
     TOK_COLON = 276,
     TOK_LP = 277,
     TOK_RP = 278,
     TOKEN = 279
   };
#endif
/* Tokens.  */
#define TOK_COMMA 258
#define TOK_COLONCOLON 259
#define TOK_COND 260
#define TOK_OR 261
#define TOK_AND 262
#define TOK_NE 263
#define TOK_LE 264
#define TOK_GE 265
#define TOK_LT 266
#define TOK_GT 267
#define TOK_EQ 268
#define TOK_MINUS 269
#define TOK_PLUS 270
#define TOK_MOD 271
#define TOK_DIV 272
#define TOK_MULT 273
#define TOK_COMPL 274
#define TOK_EQTILDE 275
#define TOK_COLON 276
#define TOK_LP 277
#define TOK_RP 278
#define TOKEN 279




/* Copy the first part of user declarations.  */
#line 1 "callweaver_expr2.y"

/* Written by Pace Willisson (pace@blitz.com) 
 * and placed in the public domain.
 *
 * Largely rewritten by J.T. Conklin (jtc@wimsey.com)
 *
 * And then overhauled twice by Steve Murphy (murf@e-tools.com)
 * to add double-quoted strings, allow mult. spaces, improve
 * error messages, and then to fold in a flex scanner for the 
 * yylex operation.
 *
 */

#include "callweaver.h"

#include <sys/types.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "callweaver/callweaver_expr.h"
#include "callweaver/logger.h"
#include "callweaver/pbx.h"


typedef void *yyscan_t;

#include "callweaver_expr2-common.h"


#define YYPARSE_PARAM parseio
#define YYLEX_PARAM ((struct parse_io *)parseio)->scanner
#define YYERROR_VERBOSE 1
extern char extra_error_message[4095];
extern int extra_error_message_supplied;

 
static struct cw_dynargs *args_new(void);
static struct cw_dynargs *args_push_null(struct cw_dynargs *arglist);
static struct cw_dynargs *args_push_val(struct cw_dynargs *arglist, struct val *vp);
static void free_args(struct cw_dynargs *);
static void free_value(struct val *);
static int is_zero_or_null(struct val *);
static int isstring(struct val *);
static struct val *make_number(long double);
static struct val *make_str(enum valtype type, const char *);
static struct val *op_and(struct val *, struct val *);
static struct val *op_colon(struct val *, struct val *);
static struct val *op_eqtilde(struct val *, struct val *);
static struct val *op_div(struct val *, struct val *);
static struct val *op_eq(struct val *, struct val *);
static struct val *op_ge(struct val *, struct val *);
static struct val *op_gt(struct val *, struct val *);
static struct val *op_le(struct val *, struct val *);
static struct val *op_lt(struct val *, struct val *);
static struct val *op_cond(struct val *, struct val *, struct val *);
static struct val *op_minus(struct val *, struct val *);
static struct val *op_negate(struct val *);
static struct val *op_compl(struct val *);
static struct val *op_ne(struct val *, struct val *);
static struct val *op_or(struct val *, struct val *);
static struct val *op_plus(struct val *, struct val *);
static struct val *op_rem(struct val *, struct val *);
static struct val *op_times(struct val *, struct val *);
static int to_number(struct val *, int silent);
static int to_string(struct val *);

/* uh, if I want to predeclare yylex with a YYLTYPE, I have to predeclare the yyltype... sigh */
typedef struct yyltype
{
  int first_line;
  int first_column;

  int last_line;
  int last_column;
} yyltype;

# define YYLTYPE yyltype
# define YYLTYPE_IS_TRIVIAL 1

/* we will get warning about no prototype for yylex! But we can't
   define it here, we have no definition yet for YYSTYPE. */

int		cw_yyerror(const char *,YYLTYPE *, struct parse_io *);
 
/* I wanted to add args to the yyerror routine, so I could print out
   some useful info about the error. Not as easy as it looks, but it
   is possible. */
#define cw_yyerror(x) cw_yyerror(x,&yyloc,parseio)


/* Enabling traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif

/* Enabling verbose error messages.  */
#ifdef YYERROR_VERBOSE
# undef YYERROR_VERBOSE
# define YYERROR_VERBOSE 1
#else
# define YYERROR_VERBOSE 0
#endif

/* Enabling the token table.  */
#ifndef YYTOKEN_TABLE
# define YYTOKEN_TABLE 0
#endif

#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
typedef union YYSTYPE
#line 106 "callweaver_expr2.y"
{
	struct val *val;
	struct cw_dynargs *args;
}
/* Line 187 of yacc.c.  */
#line 255 "callweaver_expr2.c"
	YYSTYPE;
# define yystype YYSTYPE /* obsolescent; will be withdrawn */
# define YYSTYPE_IS_DECLARED 1
# define YYSTYPE_IS_TRIVIAL 1
#endif

#if ! defined YYLTYPE && ! defined YYLTYPE_IS_DECLARED
typedef struct YYLTYPE
{
  int first_line;
  int first_column;
  int last_line;
  int last_column;
} YYLTYPE;
# define yyltype YYLTYPE /* obsolescent; will be withdrawn */
# define YYLTYPE_IS_DECLARED 1
# define YYLTYPE_IS_TRIVIAL 1
#endif


/* Copy the second part of user declarations.  */
#line 111 "callweaver_expr2.y"

extern int		cw_yylex __P((YYSTYPE *, YYLTYPE *, yyscan_t));


/* Line 216 of yacc.c.  */
#line 283 "callweaver_expr2.c"

#ifdef short
# undef short
#endif

#ifdef YYTYPE_UINT8
typedef YYTYPE_UINT8 yytype_uint8;
#else
typedef unsigned char yytype_uint8;
#endif

#ifdef YYTYPE_INT8
typedef YYTYPE_INT8 yytype_int8;
#elif (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
typedef signed char yytype_int8;
#else
typedef short int yytype_int8;
#endif

#ifdef YYTYPE_UINT16
typedef YYTYPE_UINT16 yytype_uint16;
#else
typedef unsigned short int yytype_uint16;
#endif

#ifdef YYTYPE_INT16
typedef YYTYPE_INT16 yytype_int16;
#else
typedef short int yytype_int16;
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif ! defined YYSIZE_T && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned int
# endif
#endif

#define YYSIZE_MAXIMUM ((YYSIZE_T) -1)

#ifndef YY_
# if YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(msgid) dgettext ("bison-runtime", msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(msgid) msgid
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YYUSE(e) ((void) (e))
#else
# define YYUSE(e) /* empty */
#endif

/* Identity function, used to suppress warnings about constant conditions.  */
#ifndef lint
# define YYID(n) (n)
#else
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static int
YYID (int i)
#else
static int
YYID (i)
    int i;
#endif
{
  return i;
}
#endif

#if ! defined yyoverflow || YYERROR_VERBOSE

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#     ifndef _STDLIB_H
#      define _STDLIB_H 1
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's `empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (YYID (0))
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined _STDLIB_H \
       && ! ((defined YYMALLOC || defined malloc) \
	     && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef _STDLIB_H
#    define _STDLIB_H 1
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined _STDLIB_H && (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* ! defined yyoverflow || YYERROR_VERBOSE */


#if (! defined yyoverflow \
     && (! defined __cplusplus \
	 || (defined YYLTYPE_IS_TRIVIAL && YYLTYPE_IS_TRIVIAL \
	     && defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yytype_int16 yyss;
  YYSTYPE yyvs;
    YYLTYPE yyls;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (sizeof (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (sizeof (yytype_int16) + sizeof (YYSTYPE) + sizeof (YYLTYPE)) \
      + 2 * YYSTACK_GAP_MAXIMUM)

/* Copy COUNT objects from FROM to TO.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(To, From, Count) \
      __builtin_memcpy (To, From, (Count) * sizeof (*(From)))
#  else
#   define YYCOPY(To, From, Count)		\
      do					\
	{					\
	  YYSIZE_T yyi;				\
	  for (yyi = 0; yyi < (Count); yyi++)	\
	    (To)[yyi] = (From)[yyi];		\
	}					\
      while (YYID (0))
#  endif
# endif

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack)					\
    do									\
      {									\
	YYSIZE_T yynewbytes;						\
	YYCOPY (&yyptr->Stack, Stack, yysize);				\
	Stack = &yyptr->Stack;						\
	yynewbytes = yystacksize * sizeof (*Stack) + YYSTACK_GAP_MAXIMUM; \
	yyptr += yynewbytes / sizeof (*yyptr);				\
      }									\
    while (YYID (0))

#endif

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  11
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   158

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  25
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  5
/* YYNRULES -- Number of rules.  */
#define YYNRULES  31
/* YYNRULES -- Number of states.  */
#define YYNSTATES  55

/* YYTRANSLATE(YYLEX) -- Bison symbol number corresponding to YYLEX.  */
#define YYUNDEFTOK  2
#define YYMAXUTOK   279

#define YYTRANSLATE(YYX)						\
  ((unsigned int) (YYX) <= YYMAXUTOK ? yytranslate[YYX] : YYUNDEFTOK)

/* YYTRANSLATE[YYLEX] -- Bison symbol number corresponding to YYLEX.  */
static const yytype_uint8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24
};

#if YYDEBUG
/* YYPRHS[YYN] -- Index of the first RHS symbol of rule number YYN in
   YYRHS.  */
static const yytype_uint8 yyprhs[] =
{
       0,     0,     3,     5,     6,     7,     9,    11,    15,    18,
      21,    23,    28,    30,    34,    38,    42,    46,    50,    54,
      58,    62,    66,    70,    74,    77,    80,    84,    88,    92,
      96,   100
};

/* YYRHS -- A `-1'-separated list of the rules' RHS.  */
static const yytype_int8 yyrhs[] =
{
      26,     0,    -1,    29,    -1,    -1,    -1,    28,    -1,    29,
      -1,    28,     3,    29,    -1,    28,     3,    -1,     3,    29,
      -1,     3,    -1,    24,    22,    27,    23,    -1,    24,    -1,
      22,    29,    23,    -1,    29,     6,    29,    -1,    29,     7,
      29,    -1,    29,    13,    29,    -1,    29,    12,    29,    -1,
      29,    11,    29,    -1,    29,    10,    29,    -1,    29,     9,
      29,    -1,    29,     8,    29,    -1,    29,    15,    29,    -1,
      29,    14,    29,    -1,    14,    29,    -1,    19,    29,    -1,
      29,    18,    29,    -1,    29,    17,    29,    -1,    29,    16,
      29,    -1,    29,    21,    29,    -1,    29,    20,    29,    -1,
      29,     5,    29,     4,    29,    -1
};

/* YYRLINE[YYN] -- source line where rule number YYN was defined.  */
static const yytype_uint16 yyrline[] =
{
       0,   136,   136,   145,   156,   160,   163,   167,   171,   175,
     179,   185,   212,   213,   216,   219,   222,   225,   228,   231,
     234,   237,   240,   243,   246,   249,   252,   255,   258,   261,
     264,   267
};
#endif

#if YYDEBUG || YYERROR_VERBOSE || YYTOKEN_TABLE
/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "$end", "error", "$undefined", "TOK_COMMA", "TOK_COLONCOLON",
  "TOK_COND", "TOK_OR", "TOK_AND", "TOK_NE", "TOK_LE", "TOK_GE", "TOK_LT",
  "TOK_GT", "TOK_EQ", "TOK_MINUS", "TOK_PLUS", "TOK_MOD", "TOK_DIV",
  "TOK_MULT", "TOK_COMPL", "TOK_EQTILDE", "TOK_COLON", "TOK_LP", "TOK_RP",
  "TOKEN", "$accept", "start", "args", "args1", "expr", 0
};
#endif

# ifdef YYPRINT
/* YYTOKNUM[YYLEX-NUM] -- Internal token number corresponding to
   token YYLEX-NUM.  */
static const yytype_uint16 yytoknum[] =
{
       0,   256,   257,   258,   259,   260,   261,   262,   263,   264,
     265,   266,   267,   268,   269,   270,   271,   272,   273,   274,
     275,   276,   277,   278,   279
};
# endif

/* YYR1[YYN] -- Symbol number of symbol that rule YYN derives.  */
static const yytype_uint8 yyr1[] =
{
       0,    25,    26,    26,    27,    27,    28,    28,    28,    28,
      28,    29,    29,    29,    29,    29,    29,    29,    29,    29,
      29,    29,    29,    29,    29,    29,    29,    29,    29,    29,
      29,    29
};

/* YYR2[YYN] -- Number of symbols composing right hand side of rule YYN.  */
static const yytype_uint8 yyr2[] =
{
       0,     2,     1,     0,     0,     1,     1,     3,     2,     2,
       1,     4,     1,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     2,     2,     3,     3,     3,     3,
       3,     5
};

/* YYDEFACT[STATE-NAME] -- Default rule to reduce with in state
   STATE-NUM when YYTABLE doesn't specify something else to do.  Zero
   means the default is an error.  */
static const yytype_uint8 yydefact[] =
{
       3,     0,     0,     0,    12,     0,     2,    24,    25,     0,
       4,     1,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    13,    10,
       0,     5,     6,     0,    14,    15,    21,    20,    19,    18,
      17,    16,    23,    22,    28,    27,    26,    30,    29,     9,
      11,     8,     0,     7,    31
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
      -1,     5,    30,    31,     6
};

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
#define YYPACT_NINF -17
static const yytype_int16 yypact[] =
{
      15,    15,    15,    15,   -16,    32,    84,    10,    10,    47,
      24,   -17,    15,    15,    15,    15,    15,    15,    15,    15,
      15,    15,    15,    15,    15,    15,    15,    15,   -17,    15,
      12,     7,    84,    67,   115,   129,   137,   137,   137,   137,
     137,   137,   -13,   -13,    10,    10,    10,   -17,   -17,    84,
     -17,    15,    15,    84,   100
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -17,   -17,   -17,   -17,    -1
};

/* YYTABLE[YYPACT[STATE-NUM]].  What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule which
   number is the opposite.  If zero, do what YYDEFACT says.
   If YYTABLE_NINF, syntax error.  */
#define YYTABLE_NINF -1
static const yytype_uint8 yytable[] =
{
       7,     8,     9,    23,    24,    25,    10,    26,    27,    32,
      51,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    44,    45,    46,    47,    48,    29,    49,     1,
      26,    27,    11,     0,     2,    50,     0,     3,     1,     4,
       0,     0,     0,     2,     0,     0,     3,     0,     4,     0,
      53,    54,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,     0,    26,    27,     0,
      28,    52,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,     0,    26,    27,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
      23,    24,    25,     0,    26,    27,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,     0,
      26,    27,    14,    15,    16,    17,    18,    19,    20,    21,
      22,    23,    24,    25,     0,    26,    27,    15,    16,    17,
      18,    19,    20,    21,    22,    23,    24,    25,     0,    26,
      27,    21,    22,    23,    24,    25,     0,    26,    27
};

static const yytype_int8 yycheck[] =
{
       1,     2,     3,    16,    17,    18,    22,    20,    21,    10,
       3,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    22,    23,    24,    25,    26,    27,     3,    29,    14,
      20,    21,     0,    -1,    19,    23,    -1,    22,    14,    24,
      -1,    -1,    -1,    19,    -1,    -1,    22,    -1,    24,    -1,
      51,    52,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    -1,    20,    21,    -1,
      23,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    -1,    20,    21,     5,
       6,     7,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    -1,    20,    21,     6,     7,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    -1,
      20,    21,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    -1,    20,    21,     8,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    -1,    20,
      21,    14,    15,    16,    17,    18,    -1,    20,    21
};

/* YYSTOS[STATE-NUM] -- The (internal number of the) accessing
   symbol of state STATE-NUM.  */
static const yytype_uint8 yystos[] =
{
       0,    14,    19,    22,    24,    26,    29,    29,    29,    29,
      22,     0,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    20,    21,    23,     3,
      27,    28,    29,    29,    29,    29,    29,    29,    29,    29,
      29,    29,    29,    29,    29,    29,    29,    29,    29,    29,
      23,     3,     4,    29,    29
};

#define yyerrok		(yyerrstatus = 0)
#define yyclearin	(yychar = YYEMPTY)
#define YYEMPTY		(-2)
#define YYEOF		0

#define YYACCEPT	goto yyacceptlab
#define YYABORT		goto yyabortlab
#define YYERROR		goto yyerrorlab


/* Like YYERROR except do call yyerror.  This remains here temporarily
   to ease the transition to the new meaning of YYERROR, for GCC.
   Once GCC version 2 has supplanted version 1, this can go.  */

#define YYFAIL		goto yyerrlab

#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)					\
do								\
  if (yychar == YYEMPTY && yylen == 1)				\
    {								\
      yychar = (Token);						\
      yylval = (Value);						\
      yytoken = YYTRANSLATE (yychar);				\
      YYPOPSTACK (1);						\
      goto yybackup;						\
    }								\
  else								\
    {								\
      yyerror (YY_("syntax error: cannot back up")); \
      YYERROR;							\
    }								\
while (YYID (0))


#define YYTERROR	1
#define YYERRCODE	256


/* YYLLOC_DEFAULT -- Set CURRENT to span from RHS[1] to RHS[N].
   If N is 0, then set CURRENT to the empty location which ends
   the previous symbol: RHS[0] (always defined).  */

#define YYRHSLOC(Rhs, K) ((Rhs)[K])
#ifndef YYLLOC_DEFAULT
# define YYLLOC_DEFAULT(Current, Rhs, N)				\
    do									\
      if (YYID (N))                                                    \
	{								\
	  (Current).first_line   = YYRHSLOC (Rhs, 1).first_line;	\
	  (Current).first_column = YYRHSLOC (Rhs, 1).first_column;	\
	  (Current).last_line    = YYRHSLOC (Rhs, N).last_line;		\
	  (Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
	}								\
      else								\
	{								\
	  (Current).first_line   = (Current).last_line   =		\
	    YYRHSLOC (Rhs, 0).last_line;				\
	  (Current).first_column = (Current).last_column =		\
	    YYRHSLOC (Rhs, 0).last_column;				\
	}								\
    while (YYID (0))
#endif


/* YY_LOCATION_PRINT -- Print the location on the stream.
   This macro was not mandated originally: define only if we know
   we won't break user code: when these are the locations we know.  */

#ifndef YY_LOCATION_PRINT
# if YYLTYPE_IS_TRIVIAL
#  define YY_LOCATION_PRINT(File, Loc)			\
     fprintf (File, "%d.%d-%d.%d",			\
	      (Loc).first_line, (Loc).first_column,	\
	      (Loc).last_line,  (Loc).last_column)
# else
#  define YY_LOCATION_PRINT(File, Loc) ((void) 0)
# endif
#endif


/* YYLEX -- calling `yylex' with the right arguments.  */

#ifdef YYLEX_PARAM
# define YYLEX yylex (&yylval, &yylloc, YYLEX_PARAM)
#else
# define YYLEX yylex (&yylval, &yylloc)
#endif

/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)			\
do {						\
  if (yydebug)					\
    YYFPRINTF Args;				\
} while (YYID (0))

# define YY_SYMBOL_PRINT(Title, Type, Value, Location)			  \
do {									  \
  if (yydebug)								  \
    {									  \
      YYFPRINTF (stderr, "%s ", Title);					  \
      yy_symbol_print (stderr,						  \
		  Type, Value, Location); \
      YYFPRINTF (stderr, "\n");						  \
    }									  \
} while (YYID (0))


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_value_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (!yyvaluep)
    return;
  YYUSE (yylocationp);
# ifdef YYPRINT
  if (yytype < YYNTOKENS)
    YYPRINT (yyoutput, yytoknum[yytype], *yyvaluep);
# else
  YYUSE (yyoutput);
# endif
  switch (yytype)
    {
      default:
	break;
    }
}


/*--------------------------------.
| Print this symbol on YYOUTPUT.  |
`--------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_symbol_print (FILE *yyoutput, int yytype, YYSTYPE const * const yyvaluep, YYLTYPE const * const yylocationp)
#else
static void
yy_symbol_print (yyoutput, yytype, yyvaluep, yylocationp)
    FILE *yyoutput;
    int yytype;
    YYSTYPE const * const yyvaluep;
    YYLTYPE const * const yylocationp;
#endif
{
  if (yytype < YYNTOKENS)
    YYFPRINTF (yyoutput, "token %s (", yytname[yytype]);
  else
    YYFPRINTF (yyoutput, "nterm %s (", yytname[yytype]);

  YY_LOCATION_PRINT (yyoutput, *yylocationp);
  YYFPRINTF (yyoutput, ": ");
  yy_symbol_value_print (yyoutput, yytype, yyvaluep, yylocationp);
  YYFPRINTF (yyoutput, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_stack_print (yytype_int16 *bottom, yytype_int16 *top)
#else
static void
yy_stack_print (bottom, top)
    yytype_int16 *bottom;
    yytype_int16 *top;
#endif
{
  YYFPRINTF (stderr, "Stack now");
  for (; bottom <= top; ++bottom)
    YYFPRINTF (stderr, " %d", *bottom);
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)				\
do {								\
  if (yydebug)							\
    yy_stack_print ((Bottom), (Top));				\
} while (YYID (0))


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yy_reduce_print (YYSTYPE *yyvsp, YYLTYPE *yylsp, int yyrule)
#else
static void
yy_reduce_print (yyvsp, yylsp, yyrule)
    YYSTYPE *yyvsp;
    YYLTYPE *yylsp;
    int yyrule;
#endif
{
  int yynrhs = yyr2[yyrule];
  int yyi;
  unsigned long int yylno = yyrline[yyrule];
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %lu):\n",
	     yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      fprintf (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr, yyrhs[yyprhs[yyrule] + yyi],
		       &(yyvsp[(yyi + 1) - (yynrhs)])
		       , &(yylsp[(yyi + 1) - (yynrhs)])		       );
      fprintf (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)		\
do {					\
  if (yydebug)				\
    yy_reduce_print (yyvsp, yylsp, Rule); \
} while (YYID (0))

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args)
# define YY_SYMBOL_PRINT(Title, Type, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef	YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif



#if YYERROR_VERBOSE

# ifndef yystrlen
#  if defined __GLIBC__ && defined _STRING_H
#   define yystrlen strlen
#  else
/* Return the length of YYSTR.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static YYSIZE_T
yystrlen (const char *yystr)
#else
static YYSIZE_T
yystrlen (yystr)
    const char *yystr;
#endif
{
  YYSIZE_T yylen;
  for (yylen = 0; yystr[yylen]; yylen++)
    continue;
  return yylen;
}
#  endif
# endif

# ifndef yystpcpy
#  if defined __GLIBC__ && defined _STRING_H && defined _GNU_SOURCE
#   define yystpcpy stpcpy
#  else
/* Copy YYSRC to YYDEST, returning the address of the terminating '\0' in
   YYDEST.  */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static char *
yystpcpy (char *yydest, const char *yysrc)
#else
static char *
yystpcpy (yydest, yysrc)
    char *yydest;
    const char *yysrc;
#endif
{
  char *yyd = yydest;
  const char *yys = yysrc;

  while ((*yyd++ = *yys++) != '\0')
    continue;

  return yyd - 1;
}
#  endif
# endif

# ifndef yytnamerr
/* Copy to YYRES the contents of YYSTR after stripping away unnecessary
   quotes and backslashes, so that it's suitable for yyerror.  The
   heuristic is that double-quoting is unnecessary unless the string
   contains an apostrophe, a comma, or backslash (other than
   backslash-backslash).  YYSTR is taken from yytname.  If YYRES is
   null, do not copy; instead, return the length of what the result
   would have been.  */
static YYSIZE_T
yytnamerr (char *yyres, const char *yystr)
{
  if (*yystr == '"')
    {
      YYSIZE_T yyn = 0;
      char const *yyp = yystr;

      for (;;)
	switch (*++yyp)
	  {
	  case '\'':
	  case ',':
	    goto do_not_strip_quotes;

	  case '\\':
	    if (*++yyp != '\\')
	      goto do_not_strip_quotes;
	    /* Fall through.  */
	  default:
	    if (yyres)
	      yyres[yyn] = *yyp;
	    yyn++;
	    break;

	  case '"':
	    if (yyres)
	      yyres[yyn] = '\0';
	    return yyn;
	  }
    do_not_strip_quotes: ;
    }

  if (! yyres)
    return yystrlen (yystr);

  return yystpcpy (yyres, yystr) - yyres;
}
# endif

/* Copy into YYRESULT an error message about the unexpected token
   YYCHAR while in state YYSTATE.  Return the number of bytes copied,
   including the terminating null byte.  If YYRESULT is null, do not
   copy anything; just return the number of bytes that would be
   copied.  As a special case, return 0 if an ordinary "syntax error"
   message will do.  Return YYSIZE_MAXIMUM if overflow occurs during
   size calculation.  */
static YYSIZE_T
yysyntax_error (char *yyresult, int yystate, int yychar)
{
  int yyn = yypact[yystate];

  if (! (YYPACT_NINF < yyn && yyn <= YYLAST))
    return 0;
  else
    {
      int yytype = YYTRANSLATE (yychar);
      YYSIZE_T yysize0 = yytnamerr (0, yytname[yytype]);
      YYSIZE_T yysize = yysize0;
      YYSIZE_T yysize1;
      int yysize_overflow = 0;
      enum { YYERROR_VERBOSE_ARGS_MAXIMUM = 5 };
      char const *yyarg[YYERROR_VERBOSE_ARGS_MAXIMUM];
      int yyx;

# if 0
      /* This is so xgettext sees the translatable formats that are
	 constructed on the fly.  */
      YY_("syntax error, unexpected %s");
      YY_("syntax error, unexpected %s, expecting %s");
      YY_("syntax error, unexpected %s, expecting %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s");
      YY_("syntax error, unexpected %s, expecting %s or %s or %s or %s");
# endif
      char *yyfmt;
      char const *yyf;
      static char const yyunexpected[] = "syntax error, unexpected %s";
      static char const yyexpecting[] = ", expecting %s";
      static char const yyor[] = " or %s";
      char yyformat[sizeof yyunexpected
		    + sizeof yyexpecting - 1
		    + ((YYERROR_VERBOSE_ARGS_MAXIMUM - 2)
		       * (sizeof yyor - 1))];
      char const *yyprefix = yyexpecting;

      /* Start YYX at -YYN if negative to avoid negative indexes in
	 YYCHECK.  */
      int yyxbegin = yyn < 0 ? -yyn : 0;

      /* Stay within bounds of both yycheck and yytname.  */
      int yychecklim = YYLAST - yyn + 1;
      int yyxend = yychecklim < YYNTOKENS ? yychecklim : YYNTOKENS;
      int yycount = 1;

      yyarg[0] = yytname[yytype];
      yyfmt = yystpcpy (yyformat, yyunexpected);

      for (yyx = yyxbegin; yyx < yyxend; ++yyx)
	if (yycheck[yyx + yyn] == yyx && yyx != YYTERROR)
	  {
	    if (yycount == YYERROR_VERBOSE_ARGS_MAXIMUM)
	      {
		yycount = 1;
		yysize = yysize0;
		yyformat[sizeof yyunexpected - 1] = '\0';
		break;
	      }
	    yyarg[yycount++] = yytname[yyx];
	    yysize1 = yysize + yytnamerr (0, yytname[yyx]);
	    yysize_overflow |= (yysize1 < yysize);
	    yysize = yysize1;
	    yyfmt = yystpcpy (yyfmt, yyprefix);
	    yyprefix = yyor;
	  }

      yyf = YY_(yyformat);
      yysize1 = yysize + yystrlen (yyf);
      yysize_overflow |= (yysize1 < yysize);
      yysize = yysize1;

      if (yysize_overflow)
	return YYSIZE_MAXIMUM;

      if (yyresult)
	{
	  /* Avoid sprintf, as that infringes on the user's name space.
	     Don't have undefined behavior even if the translation
	     produced a string with the wrong number of "%s"s.  */
	  char *yyp = yyresult;
	  int yyi = 0;
	  while ((*yyp = *yyf) != '\0')
	    {
	      if (*yyp == '%' && yyf[1] == 's' && yyi < yycount)
		{
		  yyp += yytnamerr (yyp, yyarg[yyi++]);
		  yyf += 2;
		}
	      else
		{
		  yyp++;
		  yyf++;
		}
	    }
	}
      return yysize;
    }
}
#endif /* YYERROR_VERBOSE */


/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

/*ARGSUSED*/
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
static void
yydestruct (const char *yymsg, int yytype, YYSTYPE *yyvaluep, YYLTYPE *yylocationp)
#else
static void
yydestruct (yymsg, yytype, yyvaluep, yylocationp)
    const char *yymsg;
    int yytype;
    YYSTYPE *yyvaluep;
    YYLTYPE *yylocationp;
#endif
{
  YYUSE (yyvaluep);
  YYUSE (yylocationp);

  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yytype, yyvaluep, yylocationp);

  switch (yytype)
    {
      case 24: /* "TOKEN" */
#line 131 "callweaver_expr2.y"
	{ free_value((yyvaluep->val)); };
#line 1241 "callweaver_expr2.c"
	break;
      case 27: /* "args" */
#line 132 "callweaver_expr2.y"
	{ free_args((yyvaluep->args)); };
#line 1246 "callweaver_expr2.c"
	break;
      case 28: /* "args1" */
#line 132 "callweaver_expr2.y"
	{ free_args((yyvaluep->args)); };
#line 1251 "callweaver_expr2.c"
	break;
      case 29: /* "expr" */
#line 131 "callweaver_expr2.y"
	{ free_value((yyvaluep->val)); };
#line 1256 "callweaver_expr2.c"
	break;

      default:
	break;
    }
}


/* Prevent warnings from -Wmissing-prototypes.  */

#ifdef YYPARSE_PARAM
#if defined __STDC__ || defined __cplusplus
int yyparse (void *YYPARSE_PARAM);
#else
int yyparse ();
#endif
#else /* ! YYPARSE_PARAM */
#if defined __STDC__ || defined __cplusplus
int yyparse (void);
#else
int yyparse ();
#endif
#endif /* ! YYPARSE_PARAM */






/*----------.
| yyparse.  |
`----------*/

#ifdef YYPARSE_PARAM
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void *YYPARSE_PARAM)
#else
int
yyparse (YYPARSE_PARAM)
    void *YYPARSE_PARAM;
#endif
#else /* ! YYPARSE_PARAM */
#if (defined __STDC__ || defined __C99__FUNC__ \
     || defined __cplusplus || defined _MSC_VER)
int
yyparse (void)
#else
int
yyparse ()

#endif
#endif
{
  /* The look-ahead symbol.  */
int yychar;

/* The semantic value of the look-ahead symbol.  */
YYSTYPE yylval;

/* Number of syntax errors so far.  */
int yynerrs;
/* Location data for the look-ahead symbol.  */
YYLTYPE yylloc;

  int yystate;
  int yyn;
  int yyresult;
  /* Number of tokens to shift before error messages enabled.  */
  int yyerrstatus;
  /* Look-ahead token as an internal (translated) token number.  */
  int yytoken = 0;
#if YYERROR_VERBOSE
  /* Buffer for error messages, and its allocated size.  */
  char yymsgbuf[128];
  char *yymsg = yymsgbuf;
  YYSIZE_T yymsg_alloc = sizeof yymsgbuf;
#endif

  /* Three stacks and their tools:
     `yyss': related to states,
     `yyvs': related to semantic values,
     `yyls': related to locations.

     Refer to the stacks thru separate pointers, to allow yyoverflow
     to reallocate them elsewhere.  */

  /* The state stack.  */
  yytype_int16 yyssa[YYINITDEPTH];
  yytype_int16 *yyss = yyssa;
  yytype_int16 *yyssp;

  /* The semantic value stack.  */
  YYSTYPE yyvsa[YYINITDEPTH];
  YYSTYPE *yyvs = yyvsa;
  YYSTYPE *yyvsp;

  /* The location stack.  */
  YYLTYPE yylsa[YYINITDEPTH];
  YYLTYPE *yyls = yylsa;
  YYLTYPE *yylsp;
  /* The locations where the error started and ended.  */
  YYLTYPE yyerror_range[2];

#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N), yylsp -= (N))

  YYSIZE_T yystacksize = YYINITDEPTH;

  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;
  YYLTYPE yyloc;

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yystate = 0;
  yyerrstatus = 0;
  yynerrs = 0;
  yychar = YYEMPTY;		/* Cause a token to be read.  */

  /* Initialize stack pointers.
     Waste one element of value and location stack
     so that they stay on the same level as the state stack.
     The wasted elements are never initialized.  */

  yyssp = yyss;
  yyvsp = yyvs;
  yylsp = yyls;
#if YYLTYPE_IS_TRIVIAL
  /* Initialize the default location before parsing starts.  */
  yylloc.first_line   = yylloc.last_line   = 1;
  yylloc.first_column = yylloc.last_column = 0;
#endif

  goto yysetstate;

/*------------------------------------------------------------.
| yynewstate -- Push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
 yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;

 yysetstate:
  *yyssp = yystate;

  if (yyss + yystacksize - 1 <= yyssp)
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYSIZE_T yysize = yyssp - yyss + 1;

#ifdef yyoverflow
      {
	/* Give user a chance to reallocate the stack.  Use copies of
	   these so that the &'s don't force the real ones into
	   memory.  */
	YYSTYPE *yyvs1 = yyvs;
	yytype_int16 *yyss1 = yyss;
	YYLTYPE *yyls1 = yyls;

	/* Each stack pointer address is followed by the size of the
	   data in use in that stack, in bytes.  This used to be a
	   conditional around just the two extra args, but that might
	   be undefined if yyoverflow is a macro.  */
	yyoverflow (YY_("memory exhausted"),
		    &yyss1, yysize * sizeof (*yyssp),
		    &yyvs1, yysize * sizeof (*yyvsp),
		    &yyls1, yysize * sizeof (*yylsp),
		    &yystacksize);
	yyls = yyls1;
	yyss = yyss1;
	yyvs = yyvs1;
      }
#else /* no yyoverflow */
# ifndef YYSTACK_RELOCATE
      goto yyexhaustedlab;
# else
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
	goto yyexhaustedlab;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
	yystacksize = YYMAXDEPTH;

      {
	yytype_int16 *yyss1 = yyss;
	union yyalloc *yyptr =
	  (union yyalloc *) YYSTACK_ALLOC (YYSTACK_BYTES (yystacksize));
	if (! yyptr)
	  goto yyexhaustedlab;
	YYSTACK_RELOCATE (yyss);
	YYSTACK_RELOCATE (yyvs);
	YYSTACK_RELOCATE (yyls);
#  undef YYSTACK_RELOCATE
	if (yyss1 != yyssa)
	  YYSTACK_FREE (yyss1);
      }
# endif
#endif /* no yyoverflow */

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;
      yylsp = yyls + yysize - 1;

      YYDPRINTF ((stderr, "Stack size increased to %lu\n",
		  (unsigned long int) yystacksize));

      if (yyss + yystacksize - 1 <= yyssp)
	YYABORT;
    }

  YYDPRINTF ((stderr, "Entering state %d\n", yystate));

  goto yybackup;

/*-----------.
| yybackup.  |
`-----------*/
yybackup:

  /* Do appropriate processing given the current state.  Read a
     look-ahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to look-ahead token.  */
  yyn = yypact[yystate];
  if (yyn == YYPACT_NINF)
    goto yydefault;

  /* Not known => get a look-ahead token if don't already have one.  */

  /* YYCHAR is either YYEMPTY or YYEOF or a valid look-ahead symbol.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token: "));
      yychar = YYLEX;
    }

  if (yychar <= YYEOF)
    {
      yychar = yytoken = YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yyn == 0 || yyn == YYTABLE_NINF)
	goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the look-ahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);

  /* Discard the shifted token unless it is eof.  */
  if (yychar != YYEOF)
    yychar = YYEMPTY;

  yystate = yyn;
  *++yyvsp = yylval;
  *++yylsp = yylloc;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- Do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     `$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];

  /* Default location.  */
  YYLLOC_DEFAULT (yyloc, (yylsp - yylen), yylen);
  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
        case 2:
#line 136 "callweaver_expr2.y"
    {
			struct parse_io *p = parseio;

			if ((p->val = malloc(sizeof(*p->val))))
				memcpy(p->val, (yyvsp[(1) - (1)].val), sizeof(*p->val));
			free((yyvsp[(1) - (1)].val));
			if (!p->val)
				YYABORT;
		;}
    break;

  case 3:
#line 145 "callweaver_expr2.y"
    {/* nothing */
			struct parse_io *p = parseio;

			if ((p->val = malloc(sizeof(*p->val)))) {
				p->val->type = CW_EXPR_string;
				p->val->u.s = strdup("");
			} else
				YYABORT;
		;}
    break;

  case 4:
#line 156 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_new()))
				YYABORT;
		;}
    break;

  case 6:
#line 163 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_val(args_new(), (yyvsp[(1) - (1)].val))))
				YYABORT;
		;}
    break;

  case 7:
#line 167 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_val((yyvsp[(1) - (3)].args), (yyvsp[(3) - (3)].val))))
				YYABORT;
		;}
    break;

  case 8:
#line 171 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_null((yyvsp[(1) - (2)].args))))
				YYABORT;
		;}
    break;

  case 9:
#line 175 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_val(args_push_null(args_new()), (yyvsp[(2) - (2)].val))))
				YYABORT;
		;}
    break;

  case 10:
#line 179 "callweaver_expr2.y"
    {
			if (!((yyval.args) = args_push_null(args_new())))
				YYABORT;
		;}
    break;

  case 11:
#line 185 "callweaver_expr2.y"
    {
			int res = 1;

			(yyval.val) = NULL;
			if (!cw_dynargs_need((yyvsp[(3) - (4)].args), 1) && to_string((yyvsp[(1) - (4)].val))) {
				const struct parse_io *p = parseio;
				struct cw_dynstr result = CW_DYNSTR_INIT;

				(yyvsp[(3) - (4)].args)->data[(yyvsp[(3) - (4)].args)->used] = NULL;

				if (!(res = (cw_function_exec(p->chan, cw_hash_string((yyvsp[(1) - (4)].val)->u.s), (yyvsp[(1) - (4)].val)->u.s, (yyvsp[(3) - (4)].args)->used, &(yyvsp[(3) - (4)].args)->data[0], &result) || result.error))) {
					free((yyvsp[(1) - (4)].val)->u.s);
					if (!(res = (!((yyvsp[(1) - (4)].val)->u.s = cw_dynstr_steal(&result)) && !((yyvsp[(1) - (4)].val)->u.s = strdup(""))))) {
						(yyvsp[(1) - (4)].val)->type = CW_EXPR_arbitrary_string;
						(yyval.val) = (yyvsp[(1) - (4)].val);
						(yyvsp[(1) - (4)].val) = NULL;
					}
				}
				cw_dynstr_free(&result);
			}

			free_value((yyvsp[(1) - (4)].val));
			free_args((yyvsp[(3) - (4)].args));

			if (res)
				YYABORT;
		;}
    break;

  case 12:
#line 212 "callweaver_expr2.y"
    { if (!((yyval.val) = (yyvsp[(1) - (1)].val))) YYABORT; ;}
    break;

  case 13:
#line 213 "callweaver_expr2.y"
    { if (!((yyval.val) = (yyvsp[(2) - (3)].val))) YYABORT;
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 14:
#line 216 "callweaver_expr2.y"
    { if (!((yyval.val) = op_or ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
                         (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 15:
#line 219 "callweaver_expr2.y"
    { if (!((yyval.val) = op_and ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
                          (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 16:
#line 222 "callweaver_expr2.y"
    { if (!((yyval.val) = op_eq ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                     (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 17:
#line 225 "callweaver_expr2.y"
    { if (!((yyval.val) = op_gt ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
                         (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column;
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 18:
#line 228 "callweaver_expr2.y"
    { if (!((yyval.val) = op_lt ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                     (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						 (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 19:
#line 231 "callweaver_expr2.y"
    { if (!((yyval.val) = op_ge ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 20:
#line 234 "callweaver_expr2.y"
    { if (!((yyval.val) = op_le ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 21:
#line 237 "callweaver_expr2.y"
    { if (!((yyval.val) = op_ne ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 22:
#line 240 "callweaver_expr2.y"
    { if (!((yyval.val) = op_plus ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 23:
#line 243 "callweaver_expr2.y"
    { if (!((yyval.val) = op_minus ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 24:
#line 246 "callweaver_expr2.y"
    { if (!((yyval.val) = op_negate ((yyvsp[(2) - (2)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (2)]).first_column; (yyloc).last_column = (yylsp[(2) - (2)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 25:
#line 249 "callweaver_expr2.y"
    { if (!((yyval.val) = op_compl ((yyvsp[(2) - (2)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (2)]).first_column; (yyloc).last_column = (yylsp[(2) - (2)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 26:
#line 252 "callweaver_expr2.y"
    { if (!((yyval.val) = op_times ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                       (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						   (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 27:
#line 255 "callweaver_expr2.y"
    { if (!((yyval.val) = op_div ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 28:
#line 258 "callweaver_expr2.y"
    { if (!((yyval.val) = op_rem ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                      (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
						  (yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 29:
#line 261 "callweaver_expr2.y"
    { if (!((yyval.val) = op_colon ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 30:
#line 264 "callweaver_expr2.y"
    { if (!((yyval.val) = op_eqtilde ((yyvsp[(1) - (3)].val), (yyvsp[(3) - (3)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (3)]).first_column; (yyloc).last_column = (yylsp[(3) - (3)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;

  case 31:
#line 267 "callweaver_expr2.y"
    { if (!((yyval.val) = op_cond ((yyvsp[(1) - (5)].val), (yyvsp[(3) - (5)].val), (yyvsp[(5) - (5)].val)))) YYABORT;
	                        (yyloc).first_column = (yylsp[(1) - (5)]).first_column; (yyloc).last_column = (yylsp[(3) - (5)]).last_column; 
							(yyloc).first_line=0; (yyloc).last_line=0;;}
    break;


/* Line 1267 of yacc.c.  */
#line 1823 "callweaver_expr2.c"
      default: break;
    }
  YY_SYMBOL_PRINT ("-> $$ =", yyr1[yyn], &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);

  *++yyvsp = yyval;
  *++yylsp = yyloc;

  /* Now `shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */

  yyn = yyr1[yyn];

  yystate = yypgoto[yyn - YYNTOKENS] + *yyssp;
  if (0 <= yystate && yystate <= YYLAST && yycheck[yystate] == *yyssp)
    yystate = yytable[yystate];
  else
    yystate = yydefgoto[yyn - YYNTOKENS];

  goto yynewstate;


/*------------------------------------.
| yyerrlab -- here on detecting error |
`------------------------------------*/
yyerrlab:
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
#if ! YYERROR_VERBOSE
      yyerror (YY_("syntax error"));
#else
      {
	YYSIZE_T yysize = yysyntax_error (0, yystate, yychar);
	if (yymsg_alloc < yysize && yymsg_alloc < YYSTACK_ALLOC_MAXIMUM)
	  {
	    YYSIZE_T yyalloc = 2 * yysize;
	    if (! (yysize <= yyalloc && yyalloc <= YYSTACK_ALLOC_MAXIMUM))
	      yyalloc = YYSTACK_ALLOC_MAXIMUM;
	    if (yymsg != yymsgbuf)
	      YYSTACK_FREE (yymsg);
	    yymsg = (char *) YYSTACK_ALLOC (yyalloc);
	    if (yymsg)
	      yymsg_alloc = yyalloc;
	    else
	      {
		yymsg = yymsgbuf;
		yymsg_alloc = sizeof yymsgbuf;
	      }
	  }

	if (0 < yysize && yysize <= yymsg_alloc)
	  {
	    (void) yysyntax_error (yymsg, yystate, yychar);
	    yyerror (yymsg);
	  }
	else
	  {
	    yyerror (YY_("syntax error"));
	    if (yysize != 0)
	      goto yyexhaustedlab;
	  }
      }
#endif
    }

  yyerror_range[0] = yylloc;

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse look-ahead token after an
	 error, discard it.  */

      if (yychar <= YYEOF)
	{
	  /* Return failure if at end of input.  */
	  if (yychar == YYEOF)
	    YYABORT;
	}
      else
	{
	  yydestruct ("Error: discarding",
		      yytoken, &yylval, &yylloc);
	  yychar = YYEMPTY;
	}
    }

  /* Else will try to reuse look-ahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:

  /* Pacify compilers like GCC when the user code never invokes
     YYERROR and the label yyerrorlab therefore never appears in user
     code.  */
  if (/*CONSTCOND*/ 0)
     goto yyerrorlab;

  yyerror_range[0] = yylsp[1-yylen];
  /* Do not reclaim the symbols of the rule which action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;	/* Each real token shifted decrements this.  */

  for (;;)
    {
      yyn = yypact[yystate];
      if (yyn != YYPACT_NINF)
	{
	  yyn += YYTERROR;
	  if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYTERROR)
	    {
	      yyn = yytable[yyn];
	      if (0 < yyn)
		break;
	    }
	}

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
	YYABORT;

      yyerror_range[0] = *yylsp;
      yydestruct ("Error: popping",
		  yystos[yystate], yyvsp, yylsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  if (yyn == YYFINAL)
    YYACCEPT;

  *++yyvsp = yylval;

  yyerror_range[1] = yylloc;
  /* Using YYLLOC is tempting, but would change the location of
     the look-ahead.  YYLOC is available though.  */
  YYLLOC_DEFAULT (yyloc, (yyerror_range - 1), 2);
  *++yylsp = yyloc;

  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", yystos[yyn], yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturn;

/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturn;

#ifndef yyoverflow
/*-------------------------------------------------.
| yyexhaustedlab -- memory exhaustion comes here.  |
`-------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  /* Fall through.  */
#endif

yyreturn:
  if (yychar != YYEOF && yychar != YYEMPTY)
     yydestruct ("Cleanup: discarding lookahead",
		 yytoken, &yylval, &yylloc);
  /* Do not reclaim the symbols of the rule which action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
		  yystos[*yyssp], yyvsp, yylsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif
#if YYERROR_VERBOSE
  if (yymsg != yymsgbuf)
    YYSTACK_FREE (yymsg);
#endif
  /* Make sure YYID is used.  */
  return YYID (yyresult);
}


#line 272 "callweaver_expr2.y"


static struct val *make_number(long double n)
{
	struct val *vp = NULL;

	if ((vp = malloc(sizeof(*vp)))) {
		vp->type = CW_EXPR_number;
		vp->u.n  = n;
	} else
		cw_log(CW_LOG_ERROR, "Out of memory!\n");

	return vp;

}

static struct val *make_str(enum valtype type, const char *s)
{
	struct val *vp;

	if ((vp = malloc(sizeof(*vp)))) {
		if ((vp->u.s = strdup(s))) {
			vp->type = type;
			return vp;
		}
		free(vp);
	}

	cw_log(CW_LOG_ERROR, "Out of memory!\n");
	return NULL;
}


static void
free_args(struct cw_dynargs *args)
{
	if (args) {
		int i;

		for (i = 0; i < args->used; i++)
			free(args->data[i]);

		cw_dynargs_free(args);
		free(args);
	}
}


static void free_value(struct val *vp)
{	
	if (vp) {
		if (vp->type != CW_EXPR_number)
			free(vp->u.s);
		free(vp);
	}
}


static int to_number(struct val *vp, int silent)
{
	char *end;
	long double n;
	int res = 0;

	if (vp) {
		switch (vp->type) {
			case CW_EXPR_arbitrary_string:
			case CW_EXPR_numeric_string:
				vp->type = CW_EXPR_string;
				errno = 0;
				n = strtold(vp->u.s, &end);
				if (end != vp->u.s && end[0] == '\0') {
					if (errno == ERANGE)
						cw_log(CW_LOG_WARNING, "Conversion of %s to number under/overflowed!\n", vp->u.s);
					free(vp->u.s);
					vp->type = CW_EXPR_number;
					vp->u.n = n;
					res = 1;
				}
				break;

			case CW_EXPR_number:
				res = 1;
				break;

			default:
				break;
		}

		if (!res && !silent && !extra_error_message_supplied)
			cw_log(CW_LOG_WARNING, "non-numeric argument: %s\n", vp->u.s);
	}

	return res;
}


static int to_string(struct val *vp)
{
	if (vp->type == CW_EXPR_number) {
		if ((vp->u.s = malloc(32))) {
			sprintf(vp->u.s, "%.18Lg", vp->u.n);
			vp->type = CW_EXPR_numeric_string;
		} else
			cw_log(CW_LOG_WARNING,"Out of memory!\n");
	}

	return vp->u.s != NULL;
}


/* return TRUE if this string is NOT a valid number */
static int isstring(struct val *vp)
{
	int ret = (vp->type == CW_EXPR_string);

	if (vp->type == CW_EXPR_arbitrary_string) {
		int i;

		vp->type = CW_EXPR_string;
		ret = 1;

		i = 0;
		if (vp->u.s[i] == '-' || vp->u.s[i] == '+') i++;
		if (isdigit(vp->u.s[i])) {
			/* [+-]?\d+(\.\d+)?[eE]-?\d+ */
			do { i++; } while (isdigit(vp->u.s[i]));
			if (vp->u.s[i] == '.')
				do { i++; } while (isdigit(vp->u.s[i]));
			if (vp->u.s[i] == 'e' || vp->u.s[i] == 'E') {
				i++;
				if (vp->u.s[i] == '-' || isdigit(vp->u.s[i]))
					do { i++; } while (isdigit(vp->u.s[i]));
			}

			if (!vp->u.s[i]) {
				vp->type = CW_EXPR_numeric_string;
				ret = 0;
			}
		} else {
			/* "nan", "NAN", "NaN", [+-]?inf(inity)? or [+-]?INF(INITY)? */
			if (((vp->u.s[0] == 'n' || vp->u.s[0] == 'N')
				&& (vp->u.s[1] == 'a' || (vp->u.s[1] == 'A' && vp->u.s[0] == 'N'))
				&& vp->u.s[2] == vp->u.s[0]
				&& !vp->u.s[3])
			|| (!strncmp(&vp->u.s[i], "inf", 3) && (!vp->u.s[i+3] || !strcmp(&vp->u.s[i+3], "inity")))
			|| (!strncmp(&vp->u.s[i], "INF", 3) && (!vp->u.s[i+3] || !strcmp(&vp->u.s[i+3], "INITY")))) {
				vp->type = CW_EXPR_numeric_string;
				ret = 0;
			}
		}
	}

	return ret;
}


static int is_zero_or_null(struct val *vp)
{
	int res;

	if (vp->type == CW_EXPR_number)
		res = (vp->u.n == 0.0L);
	else
		res = !vp->u.s
			|| vp->u.s[0] == '\0'
			|| (vp->u.s[0] == '0' && vp->u.s[1] == '\0')
			|| (vp->type != CW_EXPR_string && to_number(vp, 1) && vp->u.n == 0.0L);

	return res;
}


static struct cw_dynargs *args_new(void)
{
	struct cw_dynargs *arglist;

	if ((arglist = malloc(sizeof(struct cw_dynargs))))
		cw_dynargs_init(arglist, 1, CW_DYNARRAY_DEFAULT_CHUNK);

	return arglist;
}


static struct cw_dynargs *args_push_null(struct cw_dynargs *arglist)
{
	if (arglist) {
		if (cw_dynargs_need(arglist, 1) || !(arglist->data[arglist->used++] = strdup(""))) {
			free_args(arglist);
			arglist = NULL;
		}
	}

	return arglist;
}


static struct cw_dynargs *args_push_val(struct cw_dynargs *arglist, struct val *vp)
{
	if (arglist) {
		if (!cw_dynargs_need(arglist, 1) && to_string(vp)) {
			arglist->data[arglist->used++] = vp->u.s;
			vp->u.s = NULL;
		} else {
			free_args(arglist);
			arglist = NULL;
		}
	}

	free_value(vp);
	return arglist;
}


#undef cw_yyerror
#define cw_yyerror(x) cw_yyerror(x, YYLTYPE *yylloc, struct parse_io *parseio)

/* I put the cw_yyerror func in the flex input file,
   because it refers to the buffer state. Best to
   let it access the BUFFER stuff there and not trying
   define all the structs, macros etc. in this file! */


static struct val * op_or(struct val *a, struct val *b)
{
	struct val *r = a;

	if (is_zero_or_null(a)) {
		r = b;
		b = a;
	}

	free_value(b);
	return r;
}
		
static struct val * op_and(struct val *a, struct val *b)
{
	struct val *r = a;

	if (is_zero_or_null(a) || is_zero_or_null(b)) {
		free_value(a);
		r = make_number(0.0L);
	}

	free_value(b);
	return r;
}

static struct val * op_eq(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) == 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n == b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_gt(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string (b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) > 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n > b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_lt(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) < 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n < b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_ge(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) >= 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n >= b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_le(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) <= 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n <= b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_cond(struct val *a, struct val *b, struct val *c)
{
	struct val *r = b;

	if (is_zero_or_null(a)) {
		r = c;
		c = b;
	}

	free_value(a);
	free_value(c);

	return r;
}

static struct val * op_ne(struct val *a, struct val *b)
{
	struct val *r = NULL;

	if (isstring(a) || isstring(b)) {
		if (to_string(a) && to_string(b))
			r = make_number((long double)(strcoll(a->u.s, b->u.s) != 0));
	} else {
		to_number(a, 0);
		to_number(b, 0);
		r = make_number((long double)(a->u.n != b->u.n));
	}

	free_value(a);
	free_value(b);
	return r;
}

static struct val * op_plus(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1)) {
		r = a->u.n;
		if (to_number(b, 1))
			r += b->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_minus(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1)) {
		r = a->u.n;
		if (to_number(b, 1))
			r -= b->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_negate(struct val *a)
{
	long double r = 0.0L;

	if (to_number(a, 1))
		r = -a->u.n;

	free_value(a);
	return make_number(r);
}

static struct val * op_compl(struct val *a)
{
	struct val *v;

	v = make_number(is_zero_or_null(a));
	free_value(a);
	return v;
}

static struct val * op_times(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1) && to_number(b, 1))
		r = a->u.n * b->u.n;

	free_value(a);
	free_value(b);

	return make_number(r);
}

static struct val * op_div(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1)) {
		if (to_number(b, 1) && b->u.n != 0.0L)
			r = a->u.n / b->u.n;
		else
			r = INFINITY * a->u.n;
	}

	free_value(a);
	free_value(b);

	return make_number(r);
}
	
static struct val * op_rem(struct val *a, struct val *b)
{
	long double r = 0.0L;

	if (to_number(a, 1) && to_number(b, 1) && b->u.n != 0.0L)
		r = fmodl(a->u.n, b->u.n);

	free_value(a);
	free_value(b);

	return make_number(r);
}
	

static struct val * op_colon(struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	struct val *v = NULL;
	int eval;

	if (to_string(a) && to_string(b)) {
		if (!(eval = regcomp(&rp, b->u.s, REG_EXTENDED))) {
			/* remember that patterns are anchored to the beginning of the line */
			if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 && rm[0].rm_so == 0) {
				if (rm[1].rm_so >= 0) {
					*(a->u.s + rm[1].rm_eo) = '\0';
					v = make_str(CW_EXPR_arbitrary_string, a->u.s + rm[1].rm_so);
				} else
					v = make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
			} else {
				if (rp.re_nsub == 0)
					v = make_number(0.0L);
				else
					v = make_str(CW_EXPR_string, "");
			}

			regfree(&rp);
		} else {
			regerror(eval, &rp, errbuf, sizeof(errbuf));
			cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
			v = make_str(CW_EXPR_string, "");
		}
	}

	free_value(a);
	free_value(b);

	return v;
}
	

static struct val * op_eqtilde(struct val *a, struct val *b)
{
	regex_t rp;
	regmatch_t rm[2];
	char errbuf[256];
	int eval;
	struct val *v = NULL;

	if (to_string(a) && to_string(b)) {
		if (!(eval = regcomp(&rp, b->u.s, REG_EXTENDED))) {
			/* remember that patterns are anchored to the beginning of the line */
			if (regexec(&rp, a->u.s, (size_t)2, rm, 0) == 0 ) {
				if (rm[1].rm_so >= 0) {
					*(a->u.s + rm[1].rm_eo) = '\0';
					v = make_str(CW_EXPR_arbitrary_string, a->u.s + rm[1].rm_so);
				} else
					v = make_number((long double)(rm[0].rm_eo - rm[0].rm_so));
			} else {
				if (rp.re_nsub == 0)
					v = make_number(0.0L);
				else
					v = make_str(CW_EXPR_string, "");
			}

			regfree(&rp);
		} else {
			regerror(eval, &rp, errbuf, sizeof(errbuf));
			cw_log(CW_LOG_WARNING, "regcomp() error : %s", errbuf);
			v = make_str(CW_EXPR_string, "");
		}
	}

	free_value(a);
	free_value(b);

	return v;
}

