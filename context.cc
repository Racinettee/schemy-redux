#include "context.hh"

namespace drift
{
	namespace scheme
	{
		context::context(): env(std_env())
		{
			lex.add_keyword("define");
			lex.add_keyword("set");
			lex.add_keyword("lambda");
			lex.add_keyword("begin");
			lex.add_keyword("list");
		}
		std::unique_ptr<method> parse(environment& e, const token_array& tokens);
		void context::load_file(const string& name)
		{
			token_array tokens = lex.lex_file(name);
			
			methods.emplace_back(parse(env, tokens));
			
			auto& expressions = methods.back()->expressions;
			
			for (auto& expr : expressions)
				expr->value_function({});
		}
	}
}