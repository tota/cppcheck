/*
 * c++check - c/c++ syntax checking
 * Copyright (C) 2007 Daniel Marjamäki
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/
 */


// The preprocessor that c++check uses is a bit special. Instead of generating
// the code for a known configuration, it generates the code for each configuration.


#include <cstring>
#include "testsuite.h"
#define UNIT_TESTING        // Get access to "private" data in Tokenizer
#include "tokenize.h"

extern std::ostringstream errout;
class TestTokenizer : public TestFixture
{
public:
    TestTokenizer() : TestFixture("TestTokenizer")
    { }

private:

    void run()
    {
        TEST_CASE( multiline );
        TEST_CASE( longtok );

        TEST_CASE( inlineasm );

        TEST_CASE( dupfuncname );
    }


    bool cmptok(const char *expected[], const TOKEN *actual)
    {
        unsigned int i = 0;
        for (; expected[i] && actual; ++i, actual = actual->next)
        {
            if ( strcmp( expected[i], actual->aaaa() ) != 0)
                return false;
        }
        return (expected[i] == NULL && actual == NULL);
    }


    void multiline()
    {
        const char filedata[] = "#define str \"abc\" \\\n"
                                "            \"def\"\n";

        // tokenize..
        Tokenizer tokenizer;
        std::istringstream istr(filedata);
        tokenizer.tokenize(istr, "test.cpp");

        // Expected result..
        const char *expected[] =
        {
            "def",
            "str",
            ";",
            0
        };

        // Compare..
        ASSERT_EQUALS( true, cmptok(expected, tokenizer.tokens()) );
    }


    void longtok()
    {
        std::string filedata(10000,'a');

        // tokenize..
        Tokenizer tokenizer;
        std::istringstream istr(filedata);
        tokenizer.tokenize(istr, "test.cpp");

        // Expected result..
        ASSERT_EQUALS( std::string(10000,'a'), std::string(tokenizer.tokens()->aaaa()) );
    }


    void inlineasm()
    {
        const char filedata[] = "void foo()\n"
                                "{\n"
                                "    __asm\n"
                                "    {\n"
                                "        jmp $jump1\n"
                                "        $jump1:\n"
                                "    }\n"
                                "}\n";

        // tokenize..
        Tokenizer tokenizer;
        std::istringstream istr(filedata);
        tokenizer.tokenize(istr, "test.cpp");

        // Expected result..
        const char *expected[] =
        {
            "void",
            "foo",
            "(",
            ")",
            "{",
            "}",
            0
        };

        // Compare..
        ASSERT_EQUALS( true, cmptok(expected, tokenizer.tokens()) );
    }


    void dupfuncname()
    {
        const char code[] = "void a()\n"
                            "{ }\n"
                            "void a(int i)\n"
                            "{ }\n"
                            "void b()\n"
                            "{ }\n";
        // tokenize..
        Tokenizer tokenizer;
        std::istringstream istr(code);
        tokenizer.tokenize(istr, "test.cpp");

        tokenizer.fillFunctionList();

        ASSERT_EQUALS( 1, tokenizer._functionList.size() );
        ASSERT_EQUALS( std::string("b"), tokenizer._functionList[0]->aaaa() );
    }
};

REGISTER_TEST( TestTokenizer )
