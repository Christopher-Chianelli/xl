#ifndef CONTEXT_H
#define CONTEXT_H
// ****************************************************************************
//  context.h                       (C) 1992-2003 Christophe de Dinechin (ddd)
//                                                                 XL2 project
// ****************************************************************************
//
//   File Description:
//
//     The execution environment for XL
//
//     This defines both the compile-time environment (Context), where we
//     keep symbolic information, e.g. how to rewrite trees, and the
//     runtime environment (Runtime), which we use while executing trees
//
//
//
//
// ****************************************************************************
// This program is released under the GNU General Public License.
// See http://www.gnu.org/copyleft/gpl.html for details
// ****************************************************************************
// * File       : $RCSFile$
// * Revision   : $Revision$
// * Date       : $Date$
// ****************************************************************************
/*
  COMPILATION STRATEGY:
  
  The version of XL implemented here is a very simple language based
  on tree rewrites, designed to serve as a dynamic document description
  language (DDD), as well as a tool to implement the "larger" XL in a more
  dynamic way. Both usage models imply that the language is compiled on the
  fly, not statically. We use LLVM as a back-end, see compiler.h.

  Also, because the language is designed to manipulate program trees, which
  serve as the primary data structure, this implies that the program trees
  will exist at run-time as well. As a resut, there needs to be a
  garbage collection phase. The chosen garbage collection technique is
  based on reference counting because trees are not cyclic, so that
  ref-counting is both faster and simpler.


  PREDEFINED FORMS:

  XL is really built on a very small number of predefined forms recognized by
  the compilation phase.

    "A->B" defines a rewrite rule, rewriting A as B. The form A can be
           "guarded" by a "when" clause, e.g. "N! when N>0 -> N * (N-1)!"
    "A:B" is a type annotation, indicating that the type of A is B
    "(A)" is the same as A, allowing to override default precedence,
          and the same holds for A in an indentation (indent block)
    "A;B" is a sequence, evaluating A, then B, the value being B.
          The newline infix operator plays the same role.
    "data A" declares A as a form that cannot be reduced further.
          This can be used to declare data structures.

  The XL type system is itself based on tree shapes. For example, "integer" is
  a type that covers all Integer trees. Verifying if X has type Y is performed
  by evaluting the value X:Y.

  - In the direct type match case, this evaluates to X.
    For example, '1:integer' will evaluate directly to 1.

  - The X:Y expression may perform an implicit type conversion.
    For example, '1:real' will evaluate to 1.0 (converting 1 to real)

  - Finally, in case of type mismatch, X:Y evaluates to the same X:Y tree.

  At run-time, this is checked by xl_type_check, which returns NULL in case
  of mismatch, and the converted value otherwise.

  The compiler is allowed to special-case any such form. For example, if it
  determines that the type of a tree is "integer", it may represent it using a
  machine integer. It must however convert to/from tree when connecting to
  code that deals with less specialized trees.

  The compiler is also primed with a number of declarations such as:
     x:integer+y:integer -> [builtin]
  This tells the compiler to lookup a native compilation function. These
  functions are declared in basics.tbl (and possibly additional .tbl files)


  RUNTIME EXECUTION:

  The evaluation functions pointed to by 'code' in the Tree structure are
  designed to be invoked by normal C code using the normal C stack. This
  facilitate the interaction with other code.

  At top-level, the compiler generates only functions with the same
  prototype as eval_fn, i.e. Tree * (Tree *). The code is being
  generated on invokation of a form, and helps rewriting it, although
  attempts are made to leverage existing rewrites.

  Compiling such top-level forms invokes a number of rewrites. A
  specific rewrite can invoke multiple candidates. For example,
  consider the following factorial declaration:
     0! -> 1
     N! where N>0 -> N * (N-1)!

  In that case, code invoking N! will generate a test to check if N is 0, and
  if so invoke the first rule, otherwise invoke the second rule. The same
  principle applies when there are guarded rules, i.e. the "when N>0" clause
  will cause a test N>0 to be added guarding the second rule.

  If all these invokations fail, then the input tree is in "reduced form",
  i.e. it cannot be reduced further. If it is a non-leaf tree, then an attempt
  is made to evaluate its children.

  If no evaluation can be found for a given form that doesn't match a 'data'
  clause, the compiler will emit a diagnostic. This is not a fatal condition,
  however, and code will be generated that simply leaves the tree as is when
  that happens.


  GENERATING CODE FOR REWRITES:

  The code for a rewrite is kept in the 'code' field of the definition.
  This definition should never be evaluated directly, because the code field
  doesn't have the expected signature for xl_evaluate(). Specifically,
  it has additional Tree * parameters, corresponding to the variables
  in the pattern (the left-hand side of the rewrite).

  For example, the rewrite 'bar X:integer, Y -> foo X, Y+1' has two variables
  in its pattern, X and Y. Therefore, the code field for 'foo X, Y+1' will
  have the following signature: Tree *(Tree *self, Tree *X, Tree *Y).
  Note that bar is not a variable for the pattern. While the name 'bar' is
  being defined partially by this rewrite, it is not a variable in the pattern.

  The generated code will only be invoked if the conditions for invoking it
  are fulfilled. In the example, it will not be invoked if X is not an integer
  or for a form such as 'bar 3' because 3 doesn't match 'X:integer, Y'.


  CLOSURES:

  Closures are a way to "embed" the value of variables in a tree so that
  it can be safely passed around. For example, consider
      AtoB T ->
         A; do T; B
      foo X ->
         AtoB { write X+1 }

  In this example, the rules of the language state that 'write X+1' is not
  evaluated before being passed to 'AtoB', because nothing in the 'AtoB'
  pattern requires evaluation. However, 'write X+1' needs the value of 'X'
  to evaluate properly in 'AtoB'. Therefore, what is passed to 'AtoB' is
  a closure retaining the value of X in 'foo'. Assuming the value of X is 17,
  the closure will take the form of a prefix with the original expression
  as its first element, and the values to set as the next arguments.
  In our example, it will take the form '{ write X+1 } 17 false'.

  The code evaluating the closure is generated by CompiledUnit::CallClosure.
  To simplify the generated code, arguments are always on the left of
  the prefix, and we descend on the right.  This is why there is an arbitrary
  final 'false' that occupies the 'right' field of the last prefix.

  At runtime, closures are generated by CompiledUnit::CreateClosure,
  which is called by ArgumentMatch::CompileClosure(), a function called
  when passing arguments that may need to be embedded in a closure.


  LAZY EVALUATION:

  Trees are evaluated as late as possible (lazy evaluation). In order to
  achieve that, the compiler maintains a boolean variable per tree that may
  be evaluated lazily. This variable is allocated by CompiledUnit::NeedLazy.
  It is set when the value is evaluated, and initially cleared.
 */

#include <map>
#include <set>
#include <vector>
#include "base.h"
#include "tree.h"

XL_BEGIN

// ============================================================================
// 
//    Forward type declarations
// 
// ============================================================================

struct Context;                                 // Execution context
struct Rewrite;                                 // Tree rewrite data
struct Rewrote;                                 // Tree rewrite data
struct Errors;                                  // Error handlers
struct Compiler;                                // JIT compiler
struct CompiledUnit;                            // Compilation unit

typedef GCPtr<Context>             Context_p;
typedef GCPtr<Rewrote>             Rewrote_p;
typedef std::map<ulong, Rewrote_p> rewrote_table;// Hashing of rewrites
typedef Tree *(*native_fn) (Context *ctx, Tree *self);

typedef GCPtr<Rewrite>             Rewrite_p;

typedef std::map<text, Tree_p>     symbol_table; // Symbol table in context
typedef std::set<Symbols_p>        symbols_set;  // Set of symbol tables
typedef std::vector<Symbols_p>     symbols_list; // List of symbols table
typedef std::map<ulong, Rewrite_p> rewrite_table;// Hashing of rewrites
typedef symbol_table::iterator     symbol_iter;  // Iterator over sym table
typedef std::map<Tree_p, Symbols_p>capture_table;// Symbol capture table
typedef std::map<Tree_p, Tree_p>   value_table;  // Used for value caching
typedef value_table::iterator      value_iter;   // Used to iterate over values
typedef Tree * (*typecheck_fn) (Tree *src, Tree *value);



// ============================================================================
// 
//    Compile-time symbols and rewrites management
// 
// ============================================================================

struct Context
// ----------------------------------------------------------------------------
//   The evaluation context for a given tree
// ----------------------------------------------------------------------------
{
    Context(Context *parent = NULL);
    ~Context();

    // Adding definitions to the context
    Rewrote *           Define(Tree *form, Tree *value);

    // Rewriting things in the context
    Tree *              Evaluate(Tree *input);

    // The hash code used in the rewrite table
    static ulong        Hash(Tree *input);

    // Bind parameters in context based on arguments in form
    bool                Bind(Tree *form, Tree *value, TreeList *args = NULL);

    // Find the value that a name is bound to, or returns NULL
    Tree *              Bound(Name *name);

    // Check if two trees are equal
    static bool         EqualTrees(Tree *left, Tree *right);

public:
    Context_p           parent;
    rewrote_table       rewrotes;
    bool                hasConstants;

    GARBAGE_COLLECT(Context);
};


struct Rewrote
// ----------------------------------------------------------------------------
//   Information about a rewrite, e.g fact N -> N * fact(N-1)
// ----------------------------------------------------------------------------
//   Note that a rewrite with 'to' = NULL is used for 'data' statements
{
    Rewrote (Tree *f, Tree *t): from(f), to(t), hash(), native(NULL) {}
    ~Rewrote() {}

public:
    Tree_p              from;
    Tree_p              to;
    rewrote_table       hash;
    native_fn           native;

    GARBAGE_COLLECT(Rewrote);
};


struct Symbols
// ----------------------------------------------------------------------------
//   Holds the symbols in a given context
// ----------------------------------------------------------------------------
{
    Symbols(Symbols *s);
    ~Symbols();

    // Symbols properties
    Symbols *           Parent()                { return parent; }
    ulong               Depth();
    void                Import (Symbols *other) { imported.insert(other); }

    // Symbol management
    Tree *              Named (text name, bool deep = true);
    Tree *              Defined (text name, bool deep = true);
    Rewrite *           Rewrites()              { return rewrites; }

    // Entering symbols in the symbol table
    void                EnterName (text name, Tree *value, Tree *def = NULL);
    void                ExtendName (text name, Tree *value);
    Rewrite *           EnterRewrite(Rewrite *r);
    Rewrite *           EnterRewrite(Tree *from, Tree *to);
    Name *              Allocate(Name *varName);

    // Clearing symbol tables
    void                Clear();

    // Compiling and evaluating a tree in scope defined by these symbols
    Tree *              Compile(Tree *s, CompiledUnit &,
                                bool nullIfBad = false,
                                bool keepOtherConstants = false);
    Tree *              CompileAll(Tree *s,
                                   bool nullIfBad = false,
                                   bool keepOtherConstants = false);
    Tree *               CompileCall(text callee, TreeList &args,
                                     bool nullIfBad=false, bool cached = true);
    Infix *              CompileTypeTest(Tree *type);
    Tree *               Run(Tree *t);

    // Error handling
    Tree *               Ooops (text message,
                                Tree *a1=NULL, Tree *a2=NULL, Tree *a3=NULL);

public:
    Symbols_p           parent;
    symbol_table        names;
    symbol_table        definitions;
    Rewrite_p           rewrites;
    symbol_table        calls;
    value_table         type_tests;
    symbols_set         imported;
    Tree_p              error_handler;
    bool                has_rewrites_for_constants;

    static Symbols_p    symbols;

    GARBAGE_COLLECT(Symbols);
};


struct Rewrite
// ----------------------------------------------------------------------------
//   Information about a rewrite, e.g fact N -> N * fact(N-1)
// ----------------------------------------------------------------------------
//   Note that a rewrite with 'to' = NULL is used for 'data' statements
{
    Rewrite (Tree *f, Tree *t, Symbols *s = NULL):
        from(f), to(t), hash(), parameters(), symbols(s) {}
    ~Rewrite();

    Rewrite *           Add (Rewrite *rewrite);
    Tree *              Do(Action &a);
    Tree *              Compile(void);

public:
    Tree_p              from;
    Tree_p              to;
    rewrite_table       hash;
    TreeList            parameters;
    Symbols_p           symbols;

    GARBAGE_COLLECT(Rewrite);
};



// ============================================================================
// 
//    Compilation actions
// 
// ============================================================================

struct DeclarationAction : Action
// ----------------------------------------------------------------------------
//   Record data and rewrite declarations in the input tree
// ----------------------------------------------------------------------------
{
    DeclarationAction (Symbols *c): symbols(c) {}

    virtual Tree *Do(Tree *what);
    virtual Tree *DoInteger(Integer *what);
    virtual Tree *DoReal(Real *what);
    virtual Tree *DoText(Text *what);
    virtual Tree *DoName(Name *what);
    virtual Tree *DoPrefix(Prefix *what);
    virtual Tree *DoPostfix(Postfix *what);
    virtual Tree *DoInfix(Infix *what);
    virtual Tree *DoBlock(Block *what);

    void        EnterRewrite(Tree *defined, Tree *definition, Tree *where);

    Symbols_p symbols;
};


struct CompileAction : Action
// ----------------------------------------------------------------------------
//   Compute the input tree in the given compiled unit
// ----------------------------------------------------------------------------
{
    CompileAction (Symbols *s, CompiledUnit &, bool nullIfBad, bool keepAlt);

    virtual Tree *Do(Tree *what);
    virtual Tree *DoInteger(Integer *what);
    virtual Tree *DoReal(Real *what);
    virtual Tree *DoText(Text *what);
    virtual Tree *DoName(Name *what);
    virtual Tree *DoPrefix(Prefix *what);
    virtual Tree *DoPostfix(Postfix *what);
    virtual Tree *DoInfix(Infix *what);
    virtual Tree *DoBlock(Block *what);

    // Build code selecting among rewrites in current context
    Tree *         Rewrites(Tree *what);

    Symbols_p     symbols;
    CompiledUnit &unit;
    bool          nullIfBad;
    bool          keepAlternatives;
};


struct ParameterMatch : Action
// ----------------------------------------------------------------------------
//   Collect parameters on the left of a rewrite
// ----------------------------------------------------------------------------
{
    ParameterMatch (Symbols *s)
        : symbols(s), defined(NULL) {}

    virtual Tree *Do(Tree *what);
    virtual Tree *DoInteger(Integer *what);
    virtual Tree *DoReal(Real *what);
    virtual Tree *DoText(Text *what);
    virtual Tree *DoName(Name *what);
    virtual Tree *DoPrefix(Prefix *what);
    virtual Tree *DoPostfix(Postfix *what);
    virtual Tree *DoInfix(Infix *what);
    virtual Tree *DoBlock(Block *what);

    Symbols_p symbols;          // Symbols in which we test
    Tree_p    defined;          // Tree beind defined, e.g. 'sin' in 'sin X'
    TreeList  order;            // Record order of parameters
};


struct ArgumentMatch : Action
// ----------------------------------------------------------------------------
//   Check if a tree matches the form of the left of a rewrite
// ----------------------------------------------------------------------------
{
    ArgumentMatch (Tree *t,
                   Symbols *s, Symbols *l, Symbols *r,
                   CompileAction *comp, bool data):
        symbols(s), locals(l), rewrite(r),
        test(t), defined(NULL), compile(comp), unit(comp->unit), data(data) {}

    // Action callbacks
    virtual Tree *Do(Tree *what);
    virtual Tree *DoInteger(Integer *what);
    virtual Tree *DoReal(Real *what);
    virtual Tree *DoText(Text *what);
    virtual Tree *DoName(Name *what);
    virtual Tree *DoPrefix(Prefix *what);
    virtual Tree *DoPostfix(Postfix *what);
    virtual Tree *DoInfix(Infix *what);
    virtual Tree *DoBlock(Block *what);

    // Compile a tree
    Tree *         Compile(Tree *source);
    Tree *         CompileValue(Tree *source);
    Tree *         CompileClosure(Tree *source);

public:
    Symbols_p      symbols;     // Context in which we evaluate values
    Symbols_p      locals;      // Symbols where we declare arguments
    Symbols_p      rewrite;     // Symbols in which the rewrite was declared
    Tree_p         test;        // Tree we test
    Tree_p         defined;     // Tree beind defined, e.g. 'sin' in 'sin X'
    CompileAction *compile;     // Action in which we are compiling
    CompiledUnit  &unit;        // JIT compiler compilation unit
    bool           data;        // Is a data form
};


struct EnvironmentScan : Action
// ----------------------------------------------------------------------------
//   Collect variables in the tree that are imported from environment
// ----------------------------------------------------------------------------
{
    EnvironmentScan (Symbols *s): symbols(s) {}

    virtual Tree *Do(Tree *what);
    virtual Tree *DoInteger(Integer *what);
    virtual Tree *DoReal(Real *what);
    virtual Tree *DoText(Text *what);
    virtual Tree *DoName(Name *what);
    virtual Tree *DoPrefix(Prefix *what);
    virtual Tree *DoPostfix(Postfix *what);
    virtual Tree *DoInfix(Infix *what);
    virtual Tree *DoBlock(Block *what);

public:
    Symbols_p           symbols;        // Symbols in which we test
    capture_table       captured;       // Captured symbols
};


struct EvaluateChildren : Action
// ----------------------------------------------------------------------------
//   Build a clone of a tree, evaluating its children
// ----------------------------------------------------------------------------
{
    EvaluateChildren(Symbols *s): symbols(s)    { assert(s); }
    ~EvaluateChildren()                         {}

    virtual Tree *Do(Tree *what)                { return what; }
    virtual Tree *DoInteger(Integer *what)      { return what; }
    virtual Tree *DoReal(Real *what)            { return what; }
    virtual Tree *DoText(Text *what)            { return what; }
    virtual Tree *DoName(Name *what)            { return what; }
    virtual Tree *DoPrefix(Prefix *what);
    virtual Tree *DoPostfix(Postfix *what);
    virtual Tree *DoInfix(Infix *what);
    virtual Tree *DoBlock(Block *what);

    Tree *        Try(Tree *what);
public:
    Symbols *   symbols;
};



// ============================================================================
// 
//   LocalSave helper class
// 
// ============================================================================

template <class T>
struct LocalSave
// ----------------------------------------------------------------------------
//    Save a variable locally
// ----------------------------------------------------------------------------
{
    LocalSave (T &source, T value): reference(source), saved(source)
    {
        reference = value;
    }
    LocalSave(const LocalSave &o): reference(o.reference), saved(o.saved) {}
    LocalSave (T &source): reference(source), saved(source)
    {
    }
    ~LocalSave()
    {
        reference = saved;
    }
    operator T()        { return saved; }

    T&  reference;
    T   saved;
};



// ============================================================================
// 
//   Inline functions
// 
// ============================================================================

inline Symbols::Symbols(Symbols *s)
// ----------------------------------------------------------------------------
//   Create a "child" symbol table
// ----------------------------------------------------------------------------
    : parent(s), rewrites(NULL), error_handler(NULL),
      has_rewrites_for_constants(false)
{}


inline Symbols::~Symbols()
// ----------------------------------------------------------------------------
//   Delete all included rewrites if necessary and unlink from context
// ----------------------------------------------------------------------------
{}


inline ulong Symbols::Depth()
// ----------------------------------------------------------------------------
//    Return the depth for the current symbol table
// ----------------------------------------------------------------------------
{
    ulong depth = 0;
    for (Symbols *s = this; s; s = s->parent)
        depth++;
    return depth;
}

XL_END

extern "C"
{
    void debugs(XL::Symbols *s);
    void debugsc(XL::Symbols *s);
}

#endif // CONTEXT_H
