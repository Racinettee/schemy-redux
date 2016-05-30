#include "lex.hh"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
using namespace std;

namespace lispy
{
	namespace
	{
		static const char* tok_string[]
		{
			"eg. if",
			"int",
			"float",
			"eg 1.0",
			"eg. hello world",
			"==",
			"+,-,*,/",
			";", ":",
			"(", ")", "{", "}", "[", "]",
			",", "\" or '",
			"abc_123", "<", ">", "== or !=",
			"|,&,^", "// testing", "/* testing */",
			"null",
			"!"
		};
	}
	const std::string token_string(token_type t)
	{
		return std::string(tok_string[(int)t]);
	}
void lexer::add_keyword(const std::string& s)
{
	token_key[s] = token_type::Keyword;
}
token_array lexer::lex_input(istream& input)
{
	// The result of the lexing
	token_array tok_array;

	// Loop through the input till at the end of it and construct and array of tokens
	while(!input.eof())
	{
		if (input.peek() == ' ' || input.peek() == '\t')
		{
			input.get();
			column_index++;
			continue;
		}
		// This should be in the case of if the idententifier is found to be a number
		if(isdigit(input.peek()))
		{
			lex_number(input, tok_array);
		}
		else if(input.peek() == '\n')
		{
			input.get();
			line_number++;
			column_index = 0;
		}
		else if(isalpha(input.peek()))
		{
			lex_identifier(input, tok_array);
		}
		else
		{
			// This is probably the case that will handle symbols such as [ ] { } ( )
			int unget_count = lex_syntax(input, tok_array);
			for (int i = 0; i < unget_count; i++)
				input.unget();
		}
	}
	return tok_array;
}
token_array lexer::lex_file(const string& s)
{
	ifstream input(s);

	if(!input.is_open())
		throw(std::runtime_error(std::string("Could not open file")+s));

	return lex_input(input);
}
token_array lexer::lex_string(const string& s)
{
	istringstream input(s);

	return lex_input(input);
}
// I would like to make this case more specific to allow the dot at the end of a number
// as in my future programming language numbers could be objects too and should therefore support
// method calling syntax
void lexer::lex_number(istream& input, token_array& tok_array)
{
	// We'll start out with the number being int until we find out otherwise
	token_type tok_type = token_type::Int;
	string number = "";

	while (isdigit(input.peek()) || input.peek() == '.')
		number += input.get();

	// We will define some rules here. The only letter that may follow a number shall be f, for float
	if (isalpha(input.peek()))
	{
		string afix = "";

		while (isalpha(input.peek()))
			afix += input.get();

		if (afix == "f")
		{
			// Then ok
			tok_type = token_type::Float;
		}
		else
		{
			// Continue, because we shouldnt push an invalid token
			fprintf(stderr, "Lexing error line: %lli, indentifiers cannot be preceded by numbers\n", line_number);
			return;
		}
	}
	auto n_dots = std::count(number.begin(), number.end(), '.');

	if (n_dots == 1 && tok_type != token_type::Float)
	{
		// Double
		tok_type = token_type::Num;
	}
	else if (n_dots > 1)
	{
		// Print the error and continue because this token would be invalid
		fprintf(stderr, "Lexing error line: %lli, numbers cannot contain more than one '.'\n", line_number);
		return;
	}

	variant_ptr tok_value;
	switch(tok_type)
	{
	case token_type::Int:
		tok_value = make_shared<variant>(stoll(number));
		break;
	case token_type::Float:
	case token_type::Num:
		tok_value = make_shared<variant>(stod(number));
		break;
	}
	tok_array.emplace_back(tok_type, tok_value);
}

void lexer::lex_identifier(istream& input, token_array& tok_array)
{
	string keywd_or_ident = "";

	// Underscores are valid in identifiers
	while (isalnum(input.peek()) || input.peek() == '_')
		keywd_or_ident += input.get();

	if (token_key.find(keywd_or_ident) != token_key.end())
		// Push a token keyword
		tok_array.emplace_back(token_key[keywd_or_ident], make_shared<variant>(keywd_or_ident));
	// else, an identifier
	else
		tok_array.emplace_back(token_type::Identifier, make_shared<variant>(keywd_or_ident));
}

int lexer::lex_syntax(istream& input, token_array& tok_array)
{
	string syntax = "";

	// Nov 15, 2015 revision, this function for simplicity will only produce one token, for 1 syntax character
	// it will be easy for the parser to check for multi character syntax with the tokens eg += tok-plus tok-assign
	int characters_read = 0;
	if (!input.eof() && !is_whitespace(input.peek()) && !isalnum(input.peek()))
	{
		syntax += input.get();
		characters_read++;
	}
	else return 0;

	// If the whole syntax found is good then move on
	if (token_key.find(syntax) != token_key.end())
	{
		if (syntax=="\"" || syntax=="'")
		{
			tok_array.emplace_back(token_type::Quote, make_shared<variant>((unsigned char)syntax[0]));

			std::string output = "";
			while (!input.eof() && input.peek() != syntax[0])
				output += input.get();

			tok_array.emplace_back(token_type::Str, make_shared<variant>(output));

			if (input.peek() == syntax[0])
				tok_array.emplace_back(token_type::Quote, make_shared<variant>((unsigned char)input.get()));
			else
				printf("Unterminated string, expecting: %s", syntax.c_str());
		}
		else
		{
			tok_array.emplace_back(token_key[syntax], make_shared<variant>((unsigned char)syntax[0]));
		}
		return 0;
	}
	printf("Lexing error line: %lli, unknown syntax: %s\n", line_number, syntax.c_str());
	return 0;
}
void lexer::lex_string(std::istream& input, token_array& tok_array, char end_char)
{
	std::string output = "";
	while (input.peek() != end_char)
		output += input.get();

	tok_array.emplace_back(token_type::Str, make_shared<variant>(output));

	if (input.peek() == end_char)
		tok_array.emplace_back(token_type::Quote, make_shared<variant>((unsigned char)input.get()));
}

bool lexer::is_whitespace(char space)
{
	switch (space)
	{
	case ' ':
	case '\n':
	case '\t':
	case 0: // NULL
	case 3: // End of text
	case 4: // end of transmission
		return true;
	}
	return false;
}
lexer::lexer()
{
	// Load the keywords
	token_key["if"] = token_type::Keyword;
	token_key["while"] = token_type::Keyword;
	token_key["for"] = token_type::Keyword;
	token_key["break"] = token_type::Keyword;
	token_key["else"] = token_type::Keyword;
	// Load the syntax requirements
	token_key[";"] = token_type::Semicolon;
	token_key[":"] = token_type::Colon;
	token_key["["] = token_type::LBracket;
	token_key["]"] = token_type::RBracket;
	token_key["("] = token_type::LParen;
	token_key[")"] = token_type::RParen;
	token_key["{"] = token_type::LBrace;
	token_key["}"] = token_type::RBrace;
	token_key["<"] = token_type::LThan;
	token_key[">"] = token_type::GThan;
	token_key[","] = token_type::Comma;
	token_key["\""] = token_type::Quote;
	token_key["'"] = token_type::Quote;
	//
	token_key["=="] = token_type::Conditional;
	token_key["!="] = token_type::Conditional;
	token_key["<="] = token_type::Conditional;
	token_key[">="] = token_type::Conditional;
	token_key["&&"] = token_type::Conditional;
	token_key["||"] = token_type::Conditional;
	//
	token_key["&"] = token_type::Bitwise;
	token_key["|"] = token_type::Bitwise;
	token_key["^"] = token_type::Bitwise;
	//
	token_key["+"] = token_type::Arith;
	token_key["-"] = token_type::Arith;
	token_key["*"] = token_type::Arith;
	token_key["/"] = token_type::Arith;
	//
	token_key["!"] = token_type::Not;
}
}