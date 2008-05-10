
//---------------------------------------------------------------------------
// Buffer overrun..
//---------------------------------------------------------------------------

#include "CheckBufferOverrun.h"
#include "tokenize.h"
#include "CommonCheck.h"

#include <algorithm>
#include <sstream>
#include <list>

#include <stdlib.h>     // <- strtoul

//---------------------------------------------------------------------------
extern bool ShowAll;
//---------------------------------------------------------------------------

// CallStack used when parsing into subfunctions.
static std::list<const TOKEN *> CallStack;


// Modified version of 'ReportError' that also reports the callstack
static void ReportError(const TOKEN *tok, const char errmsg[])
{
    std::ostringstream ostr;
    std::list<const TOKEN *>::const_iterator it;
    for ( it = CallStack.begin(); it != CallStack.end(); it++ )
        ostr << FileLine(*it) << " -> ";
    ostr << FileLine(tok) << ": " << errmsg;
    ReportErr(ostr.str());
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// Check array usage..
//---------------------------------------------------------------------------

static void CheckBufferOverrun_CheckScope( const TOKEN *tok, const char *varname[], const int size, const int total_size )
{
    unsigned int varc = 1;
    while ( varname[varc] )
        varc++;
    varc = 2 * ( varc - 1 );


    // Array index..
    if ( Match(tok, "%var1% [ %num% ]", varname) )
    {
        const char *num = getstr(tok, 2 + varc);
        if (strtol(num, NULL, 10) >= size)
        {
            ReportError(tok->next, "Array index out of bounds");
        }
    }


    int indentlevel = 0;
    for ( ; tok; tok = tok->next )
    {
        if (tok->str[0]=='{')
        {
            indentlevel++;
        }

        else if (tok->str[0]=='}')
        {
            indentlevel--;
            if ( indentlevel < 0 )
                return;
        }

        // Array index..
        if ( !IsName(tok->str) && tok->str[0] != '.' && Match(tok->next, "%var1% [ %num% ]", varname) )
        {
            const char *num = getstr(tok->next, 2 + varc);
            if (strtol(num, NULL, 10) >= size)
            {
                ReportError(tok->next, "Array index out of bounds");
            }
            tok = gettok(tok, 4);
            continue;
        }


        // memset, memcmp, memcpy, strncpy, fgets..
        if (strcmp(tok->str,"memset")==0 ||
            strcmp(tok->str,"memcpy")==0 ||
            strcmp(tok->str,"memmove")==0 ||
            strcmp(tok->str,"memcmp")==0 ||
            strcmp(tok->str,"strncpy")==0 ||
            strcmp(tok->str,"fgets")==0 )
        {
            if ( Match( tok->next, "( %var1% , %num% , %num% )", varname ) ||
                 Match( tok->next, "( %var% , %var1% , %num% )", varname ) )
            {
                const char *num  = getstr(tok, varc + 6);
                if ( atoi(num) > total_size )
                {
                    ReportError(tok, "Buffer overrun");
                }
            }
            continue;
        }


        // Loop..
        if ( Match(tok, "for (") )
        {
            const TOKEN *tok2 = gettok( tok, 2 );

            // for - setup..
            if ( Match(tok2, "%var% = 0 ;") )
                tok2 = gettok(tok2, 4);
            else if ( Match(tok2, "%type% %var% = 0 ;") )
                tok2 = gettok(tok2, 5);
            else if ( Match(tok2, "%type% %type% %var% = 0 ;") )
                tok2 = gettok(tok2, 6);
            else
                continue;

            // for - condition..
            if ( ! Match(tok2, "%var% < %num% ;") && ! Match(tok2, "%var% <= %num% ;"))
                continue;

            // Get index variable and stopsize.
            const char *strindex = tok2->str;
            int value = (tok2->next->str[1] ? 1 : 0) + atoi(getstr(tok2, 2));
            if ( value <= size )
                continue;

            // Goto the end of the for loop..
            while (tok2 && strcmp(tok2->str,")"))
                tok2 = tok2->next;
            if (!gettok(tok2,5))
                break;

            std::ostringstream pattern;
            pattern << "%var1% [ " << strindex << " ]";

            int indentlevel2 = 0;
            while (tok2)
            {
                if ( tok2->str[0] == ';' && indentlevel2 == 0 )
                    break;

                if ( tok2->str[0] == '{' )
                    indentlevel2++;

                if ( tok2->str[0] == '}' )
                {
                    indentlevel2--;
                    if ( indentlevel2 <= 0 )
                        break;
                }

                if ( Match( tok2, pattern.str().c_str(), varname ) )
                {
                    ReportError(tok2, "Buffer overrun");
                    break;
                }

                tok2 = tok2->next;
            }
            continue;
        }


        // Writing data into array..
        if ( Match(tok, "strcpy ( %var1% , %str% )", varname) )
        {
            int len = 0;
            const char *str = getstr(tok, varc + 4 );
            while ( *str )
            {
                if (*str=='\\')
                    str++;
                str++;
                len++;
            }
            if (len > 2 && len >= (int)size + 2)
            {
                 ReportError(tok, "Buffer overrun");
            }
            continue;
        }


        // Function call..
        // It's not interesting to check what happens when the whole struct is
        // sent as the parameter, that is checked separately anyway.
        if ( Match( tok, "%var% (" ) )
        {
            // Don't make recursive checking..
            if (std::find(CallStack.begin(), CallStack.end(), tok) != CallStack.end())
                continue;

            unsigned int parlevel = 0, par = 0;
            for ( const TOKEN *tok2 = tok; tok2; tok2 = tok2->next )
            {
                if ( tok2->str[0] == '(' )
                {
                    parlevel++;
                }

                else if ( tok2->str[0] == ')' )
                {
                    parlevel--;
                    if ( parlevel < 1 )
                    {
                        par = 0;
                        break;
                    }
                }

                else if ( parlevel == 1 && tok2->str[0] == ',' )
                {
                    par++;
                }

                if ( parlevel == 1 && Match(tok2, "[(,] %var1% [,)]", varname) )
                {
                    par++;
                    break;
                }
            }

            if ( par == 0 )
                continue;

            // Find function..
            const TOKEN *ftok = GetFunctionTokenByName( tok->str );
            if ( ! ftok )
                continue;

            // Parse head of function..
            ftok = gettok( ftok, 2 );
            parlevel = 1;
            while ( ftok && parlevel == 1 && par >= 1 )
            {
                if ( ftok->str[0] == '(' )
                    parlevel++;

                else if ( ftok->str[0] == ')' )
                    parlevel--;

                else if ( ftok->str[0] == ',' )
                    par--;

                else if (par==1 && parlevel==1 && (Match(ftok, "%var% ,") || Match(ftok, "%var% )")))
                {
                    // Parameter name..
                    const char *parname[2];
                    parname[0] = ftok->str;
                    parname[1] = 0;

                    // Goto function body..
                    while ( ftok && ftok->str[0] != '{' )
                        ftok = ftok->next;
                    ftok = ftok ? ftok->next : 0;

                    // Check variable usage in the function..
                    CallStack.push_back( tok );
                    CheckBufferOverrun_CheckScope( ftok, parname, size, total_size );
                    CallStack.pop_back();

                    // break out..
                    break;
                }

                ftok = ftok->next;
            }
        }
    }
}


//---------------------------------------------------------------------------
// Checking local variables in a scope
//---------------------------------------------------------------------------

static void CheckBufferOverrun_LocalVariable()
{
    int indentlevel = 0;
    for (const TOKEN *tok = tokens; tok; tok = tok->next)
    {
        if (tok->str[0]=='{')
            indentlevel++;

        else if (tok->str[0]=='}')
            indentlevel--;

        else if (indentlevel > 0 && Match(tok, "%type% %var% [ %num% ] ;"))
        {
            const char *varname[2];
            varname[0] = getstr(tok,1);
            varname[1] = 0;
            unsigned int size = strtoul(getstr(tok,3), NULL, 10);
            int total_size = size * SizeOfType(tok->str);
            if (total_size == 0)
                continue;

            // The callstack is empty
            CallStack.clear();
            CheckBufferOverrun_CheckScope( gettok(tok,5), varname, size, total_size );
        }
    }
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// Checking member variables of structs..
//---------------------------------------------------------------------------

static void CheckBufferOverrun_StructVariable()
{
    const char *declstruct_pattern[] = {"","","{",0};
    for ( const TOKEN * tok = findtoken( tokens, declstruct_pattern );
          tok;
          tok = findtoken( tok->next, declstruct_pattern ) )
    {
        if ( strcmp(tok->str, "struct") && strcmp(tok->str, "class") )
            continue;

        const char *structname = tok->next->str;

        if ( ! IsName( structname ) )
            continue;

        // Found a struct declaration. Search for arrays..
        for ( TOKEN * tok2 = tok->next->next; tok2; tok2 = tok2->next )
        {
            if ( tok2->str[0] == '}' )
                break;

            if ( strchr( ";{,(", tok2->str[0] ) )
            {
                // Declare array..
                if ( Match(tok2->next, "%type% %var% [ %num% ] ;") ||
                     Match(tok2->next, "%type% * %var% [ %num% ] ;") )
                {
                    const char *varname[3] = {0,0,0};
                    int ivar = IsName(getstr(tok2, 2)) ? 2 : 3;

                    varname[1] = getstr(tok2, ivar);
                    int arrsize = atoi(getstr(tok2, ivar+2));
                    int total_size = arrsize * SizeOfType(tok2->next->str);
                    if (total_size == 0)
                        continue;

                    for ( const TOKEN *tok3 = tokens; tok3; tok3 = tok3->next )
                    {
                        if ( strcmp(tok3->str, structname) )
                            continue;

                        // Declare variable: Fred fred1;
                        if ( Match( tok3->next, "%var% ;" ) )
                            varname[0] = getstr(tok3, 1);

                        // Declare pointer: Fred *fred1
                        else if ( Match(tok3->next, "* %var% [,);=]") )
                            varname[0] = getstr(tok3, 2);

                        else
                            continue;


                        // Goto end of statement.
                        const TOKEN *CheckTok = NULL;
                        while ( tok3 )
                        {
                            // End of statement.
                            if ( tok3->str[0] == ';' )
                            {
                                CheckTok = tok3;
                                break;
                            }

                            // End of function declaration..
                            if ( Match(tok3, ") ;") )
                                break;

                            // Function implementation..
                            if ( Match(tok3, ") {") )
                            {
                                CheckTok = gettok(tok3, 2);
                                break;
                            }

                            tok3 = tok3->next;
                        }

                        if ( ! CheckTok )
                            continue;

                        // Check variable usage..
                        CheckBufferOverrun_CheckScope( CheckTok, varname, arrsize, total_size );
                    }
                }
            }
        }
    }
}
//---------------------------------------------------------------------------

void CheckBufferOverrun()
{
    CheckBufferOverrun_LocalVariable();
    CheckBufferOverrun_StructVariable();
}
//---------------------------------------------------------------------------








//---------------------------------------------------------------------------
// Dangerous functions
//---------------------------------------------------------------------------

void WarningDangerousFunctions()
{
    for (const TOKEN *tok = tokens; tok; tok = tok->next)
    {
        if (Match(tok, "gets ("))
        {
            std::ostringstream ostr;
            ostr << FileLine(tok) << ": Found 'gets'. You should use 'fgets' instead";
            ReportErr(ostr.str());
        }

        else if (Match(tok, "scanf (") && strcmp(getstr(tok,2),"\"%s\"") == 0)
        {
            std::ostringstream ostr;
            ostr << FileLine(tok) << ": Found 'scanf'. You should use 'fgets' instead";
            ReportErr(ostr.str());
        }
    }
}
//---------------------------------------------------------------------------




