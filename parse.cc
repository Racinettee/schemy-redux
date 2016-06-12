#include <numeric>
#include <stdexcept>
#include <cstdio>
#include "variant.hh"
#include "tokens.hh"
#include "env.hh"
using namespace std;
namespace drift
{
	namespace scheme
	{
		namespace
		{
			// Thrown if a token was expected but something else was found
			struct unexpected_token : public invalid_argument
			{
				unexpected_token(const string& msg) : invalid_argument("Unexpected token: " + msg) { }
			};
			// Thrown in the case that a parsed expression is found to be over but we were parsing elements
			struct expr_over : exception { };
			// Asserts that the token expected is present and increments the pointer
			static void expect_fn(token_type tok_ty, const token_array& tok_array, size_t& ptr, const std::string& message)
			{
				try
				{
					if (tok_ty != tok_array.at(ptr++).first)
						throw unexpected_token("expected: " + token_string(tok_ty) + " but got: " + token_string(tok_array[ptr].first) + message);
				}
				catch (out_of_range& e)
				{
					throw unexpected_token("expected: " + token_string(tok_ty) + " but caught out of bounds exception" + e.what());
				}
			}
			// Meant only for optional syntax
			inline static void optional(token_type tok_ty, const token_array& tok_array, size_t& ptr)
			{
				if (tok_ty == tok_array.at(ptr).first)
					++ptr;
			}
	#define expect(tok, exparry, ptr, msg) expect_fn(tok, exparry, ptr, string(msg)+" "+string(__FILE__)+":"+std::to_string(__LINE__))

			static variant_ptr parse_expr(method* fn, const token_array& tok_array, size_t& pos);
			static variant_ptr parse_element(method* fn, const token_array& tok_array, size_t& pos, size_t& num_args, bool lookup_identifiers = true)
			{
				while (tok_array.at(pos).first == Quote) pos++;
				auto& tok = tok_array.at(pos);

				switch (tok.first)
				{
				case Int: case Str:	case Num:
					return tok.second;
				case Identifier:
				{
					string identifier = tok.second->value_string;
					// Maybe arguments can be passed down to this function to solve the issue of de-referencing predicates
					return (lookup_identifiers ? make_variant([fn, identifier](const list& args) {
						auto found = fn->env->lookup(identifier);
						return found->variant_kind == variant::kind_function ? found->value_function(args) : found; //return fn->env->lookup(identifier);
					}) : tok.second);
				}
				case LParen:
				{
					auto res = parse_expr(fn, tok_array, pos); pos--; // We get incremented to far after parsing another expr
					return res;
				}
				case RParen:
					throw expr_over();
				default:
					return parse_element(fn, tok_array, ++pos, num_args);
				}
			}
			static void parse_condition(const token& first_tok, method* fn, const token_array& tok_array, size_t& pos)
			{
				size_t num_args = 0;

				for (; tok_array.at(pos).first != token_type::RParen; pos++, ++num_args)
					parse_element(fn, tok_array, pos, num_args);

				if (num_args > 2)
					printf("Warning, comparison should only have 2 arguments");

				switch (first_tok.first)
				{
				case LThan:
				case GThan:
					//fn->instructions.push_back(cmp_ineq);
					break;
				}
			}
			static variant_ptr parse_arith_expr(const token& first_tok, method* fn, const token_array& tok_array, size_t& pos)
			{
				size_t num_args = 0;

				list values;

				try
				{
					for (; tok_array.at(pos).first != token_type::RParen; pos++, ++num_args)
						values.emplace_back(parse_element(fn, tok_array, pos, num_args));
				}
				catch (out_of_range&) {	}
				catch (expr_over&) { }

				switch (first_tok.second->value_char)
				{
				case '+':
					return make_variant([values](const list&)->variant_ptr{
						return accumulate(values.begin(), values.end(),
							(values.front()->variant_kind == variant::kind_string ? make_variant(string("")) : make_variant(0LL)));
					});
				case '*':
					return make_variant([values](const list&) {
						auto initial = values.at(0) * values.at(1);
						for (size_t i = 2; i < values.size(); i++)
							initial = initial * values[i];
						return initial;
					});
				}
				return make_variant(variant::null_kind());
			}
			inline static list parse_args(const token_array& tok_array, size_t& pos)
			{
				expect(token_type::LParen, tok_array, pos, " while seeking arguments for a lambda");
				list args;
				for(; tok_array.at(pos).first != token_type::RParen; pos++)
					args.emplace_back(tok_array[pos].second);
				expect(token_type::RParen, tok_array, pos, " while seeking to end the arguments for a lambda");
				return args;
			}
			static variant_ptr parse_kw_expr(const token& first_tok, method* fn, const token_array& tok_array, size_t& pos)
			{
				size_t num_args = 0;
				const string& command = first_tok.second->value_string;
				if (command == "set")
					optional(Not, tok_array, pos);
				bool will_lookup = !(command == "define" ||
									command == "set" ||
									command == "lambda");

				list arguments;
				if(command == "lambda")
					arguments = parse_args(tok_array, pos);

				list values;
				try
				{
					for (; tok_array.at(pos).first != token_type::RParen; pos++, ++num_args)
						values.emplace_back(parse_element(fn, tok_array, pos, num_args, will_lookup));
				}
				catch (out_of_range&) { --pos;  }
				catch (expr_over&) { }

				if (command == "if")
				{
					if (num_args != 3)
						throw invalid_argument(string("Expected 3 arguments, but got ") + to_string(num_args));
					return make_variant([values](const list&) -> variant_ptr {
						auto& cond = *values[0];
						switch(cond.variant_kind)
						{
						case variant::kind::kind_function:
						{
							auto& value = (cond.value_function({})->value_bool ? values[1] : values[2]);
							return value->variant_kind == variant::kind_function ? value->value_function({}) : value;
						}
						case variant::kind::kind_int: case variant::kind::kind_bool:
						{
							auto& value = (cond.value_bool ? values[1] : values[2]);
							return value->variant_kind == variant::kind_function ? value->value_function({}) : value;
						}
						}
						// Only the explicit value of 0 or false can be used to evaluate false
						return make_variant(true);
					});
				}
				else if(command == "define")
				{
					if (num_args != 2)
						throw invalid_argument(string("define expects 2 arguments, but got ") + to_string(num_args));
					
					auto env = fn->env;
					return make_variant([env, values](const list&) -> variant_ptr {
						// values[0] should be an identifier, values[1] will be its value
						if (values[0]->variant_kind != variant::kind_string)
							throw invalid_argument(string("define expects an identifier as its first arg but got: ") +
								variant::kind_str(values[0]->variant_kind));

						auto& symbols = env->symbols;
						// Assert that the symbol we're defining is not already defined
						if (symbols.find(values[0]->value_string) != symbols.end())
							throw invalid_argument(values[0]->value_string + " was already defined");

						auto iter = symbols.emplace(values[0]->value_string, values[1]);

						return iter.first->second;
					});
				}
				else if (command == "set")
				{
					if (num_args != 2)
						throw invalid_argument(string("set expects 2 arguments, but got ") + to_string(num_args));

					return make_variant([fn, values](const list&) -> variant_ptr {
						auto env = fn->env;
						if (values[0]->variant_kind != variant::kind_string)
							throw invalid_argument("set expects an identifier as its first arg but got: " + variant::kind_str(values[0]->variant_kind));

						auto& symbols = env->symbols;
						// Assert that the symbol we're setting has been defined
						if (symbols.find(values[0]->value_string) == symbols.end())
							throw invalid_argument("set being used on identifier that hasn't been defined: " + values[0]->value_string);

						return symbols[values[0]->value_string] = (values[1]->variant_kind == variant::kind_function ?
							values[1]->value_function({}) :  values[1]);
					});
				}
				else if (command == "lambda")
				{
					if (num_args < 1)
						throw invalid_argument("Lambda requires at least 1 expression");
					
					//auto new_env = make_shared<environment>(fn->env);
					return make_variant([fn, arguments, values](const list& args) -> variant_ptr {
						environment env(fn->env);
						environment* original = fn->env;
						const size_t argsc = args.size();
						const size_t argumentsc = arguments.size();
						
						if(argsc != argumentsc)
							printf("Warning: Number of arguments passed to lambda do "
									"not match what it was specified with: %lu vs %lu\n", argsc, argumentsc);
							
						for(size_t i = 0UL; i < argsc; i++)
							env.symbols[arguments[i]->value_string] = args[i];
						
						fn->env = &env;
						
						for(size_t i = 0; i < values.size()-1; i++)
							if(values[i]->variant_kind == variant::kind_function)
								values[i]->value_function({});
						auto& value = values.back();
						
						auto result = (value->variant_kind == variant::kind_function ? value->value_function({}) : value);
						
						fn->env = original;
						
						return result;
					});
				}
				else if (command == "begin")
				{
					return make_variant([values](const list&) -> variant_ptr {
						for(size_t i = 0; i < values.size()-1; i++)
							if(values[i]->variant_kind == variant::kind_function)
								values[i]->value_function({});
						auto& value = values.back();
						return (value->variant_kind == variant::kind_function ? value->value_function({}) : value);
					});
				}
				else if (command == "list")
				{
					return make_variant([values](const list&) -> variant_ptr {
						list result;
						for(auto& value : values)
							result.push_back(value->variant_kind == variant::kind_function ? value->value_function({}) : value);
						return make_variant(result);
					});
				}
				return make_variant(variant::null_kind());
			}
			static variant_ptr parse_fn_call_expr(const token& first_tok, method* fn, const token_array& tok_array, size_t& pos)
			{
				size_t num_args = 0;
				list values;
				try
				{
					for (; tok_array.at(pos).first != token_type::RParen; pos++, ++num_args)
						values.emplace_back(parse_element(fn, tok_array, pos, num_args));
				}
				catch (out_of_range&) { --pos; }
				catch (expr_over&) { }

				//auto env = fn->env;
				auto name = first_tok.second->value_string;
				return make_variant([fn, name, values](const list& args)->variant_ptr{
					auto* env = fn->env;
					
					auto result = env->lookup(name);
					
					return result->variant_kind == variant::kind_function ? result->value_function(values) : result;
				});
			}
			static variant_ptr parse_expr(method* fn, const token_array& tok_array, size_t& pos)
			{
				expect(token_type::LParen, tok_array, pos, " when staring to parse expression");

				variant_ptr result;

				try
				{
					const token& first_tok = tok_array.at(pos++);
					switch (first_tok.first)
					{
					case Arith:
						result = parse_arith_expr(first_tok, fn, tok_array, pos);
						break;

					case Keyword:
						result = parse_kw_expr(first_tok, fn, tok_array, pos);
						break;

					case Identifier:
						result = parse_fn_call_expr(first_tok, fn, tok_array, pos);
					case LThan:
						break;
					case RParen:
						// We got an empty set of paren's
						pos--;
						break;
					}
				}
				catch (out_of_range& e)
				{
					std::fprintf(stderr, "Caught: %s %s\n", e.what(), " when parsing expression");
				}

				expect(token_type::RParen, tok_array, pos, " when trying to finish parsing an expression");

				return result;
			}
		}
		unique_ptr<method> parse(environment& e, const token_array& tokens)
		{
			size_t pos = 0;
			unique_ptr<method> fn = make_unique<method>(); fn->env = &e;
			while (pos < tokens.size())
				fn->expressions.push_back(parse_expr(fn.get(), tokens, pos));
			return move(fn);
		}
	}
}