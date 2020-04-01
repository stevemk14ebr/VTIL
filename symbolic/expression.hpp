﻿#pragma once
#define SYMEX_IMPLICIT_RESIZE 1

#include <vector>
#include <optional>
#include <functional>
#include "variable.hpp"
#include "operators.hpp"

namespace vtil::symbolic
{
	struct expression;
	using symbol_set = std::set<unique_identifier>;
	using symbol_map = std::map<variable, expression>;
	using fn_exp_callback = std::function<void( expression& )>;
	using fn_const_exp_callback = std::function<void( const expression& )>;

	// Describes an expression tree.
	//
	struct expression
	{
		// If variable, the value of the expression.
		//
		std::optional<variable> value;

		// If result, list of operands and the operator description.
		//
		const operator_desc* fn;
		std::vector<expression> operands;

		// Default constructors.
		//
		expression() : fn( nullptr ) {}
		expression( const variable& a ) : value( a ), fn( nullptr ) {}
		expression( const operator_desc* fn, const expression& a ) : operands( { a } ), fn( fn ) {}
		expression( const expression& a, const operator_desc* fn, const expression& b ) : operands( { a, b } ), fn( fn ) {}

		// Helpers to determine the type of the expression.
		//
		bool is_expression() const { return operands.size() && !value; }
		bool is_variable() const { return operands.empty() && value; }
		bool is_constant() const { return is_variable() && value->is_constant(); }
		bool is_valid() const { return is_variable() ? operands.empty() : fn && ( fn->is_unary ? 1 : 2 ) == operands.size(); }

		// Calls the callback provided for every expression that
		// is actually a boxed symbolic variable.
		//
		void enum_symbols( const fn_exp_callback& cb )
		{
			if ( is_variable() )
			{
				if ( value->is_symbolic() )
					cb( *this );
				return;
			}
			for ( auto& op : operands )
				op.enum_symbols( cb );
		}

		// Calls the callback provided for every expression that
		// is actually a boxed symbolic variable. [Const]
		//
		void enum_symbols( const fn_const_exp_callback& cb ) const
		{
			if ( is_variable() )
			{
				if ( value->is_symbolic() )
					cb( *this );
				return;
			}
			for ( auto& op : operands )
				enum_symbols( cb );
		}

		// Creates a set containing every unique symbol used in
		// this expression tree.
		//
		symbol_set enum_symbols() const
		{
			symbol_set tmp;
			enum_symbols( [ & ] ( auto& x ) { tmp.insert( x.value->uid ); } );
			return tmp;
		}

		// Counts the number of unique symbols used in this
		// expression tree.
		//
		size_t count_symbols() const
		{
			return enum_symbols().size();
		}

		// Remaps every occurance of the symbol<uid> with the 
		// expression provided.
		//
		void remap_symbol( const unique_identifier& uid, const expression& as )
		{
			enum_symbols( [ & ]( expression& v )
			{
				if ( v.value->uid == uid )
					v = as;
			} );
		}

		// Returns the size of the output value.
		//
		uint8_t size() const
		{
			// If variable, return size as is.
			//
			if ( is_variable() )
				return value->size;

			// If unary operator or result size is first operand,
			// redirect to the first operand.
			//
			if ( fn->is_unary || fn->result_size == 0 )
				return operands[ 0 ].size();

			// If any of the operands contain a <any_size> variable,
			// return the other alternative.
			//
			uint8_t s0 = operands[ 0 ].size();
			uint8_t s1 = operands[ 1 ].size();
			if ( !s0 ) return s1;
			if ( !s1 ) return s0;

			// Process according to the operator definition.
			//
			if ( fn->result_size == 1 )
				return std::max( s0, s1 );
			else
				return std::min( s0, s1 );
		}

		// Changes the size of the output value.
		//
		void resize( uint8_t size )
		{
			// If variable, resize it:
			//
			if ( is_variable() )
			{
				value->size = size;
				if( value->is_symbolic() )
					fassert( SYMEX_IMPLICIT_RESIZE );
				else
					value->u64 = value->get( size );
			}
			// If result of an operator:
			//
			else
			{
				// If unary operator or result size is first operand,
				// redirect to the first operand.
				//
				if ( fn->is_unary || fn->result_size == 0 )
				{
					operands[ 0 ].resize( size );
				}
				// Otherwise, resize both.
				//
				else
				{
					operands[ 0 ].resize( size );
					operands[ 1 ].resize( size );
				}
			}
		}

		// Returns an arbitrary value that represents the "complexity"
		// of the expression. It's used for the simplification algorithm.
		//
		size_t complexity() const
		{
			// If it's a variable:
			//
			if ( is_variable() )
				return value->is_constant() ? 0 : 1;

			// Exceptional case for new:
			//
			if ( fn->function == "new" )
				return operands[ 0 ].complexity();

			// Ideally we want less operations on symbolic variables, 
			// so create an exponentially increasing cost.
			//
			if ( fn->is_unary )
			{
				return operands[ 0 ].complexity() << 1;
			}
			else
				return ( operands[ 0 ].complexity() + operands[ 1 ].complexity() ) << 2;
		}

		// Depth of the operation tree.
		//
		size_t depth() const
		{
			// If variable, return 1.
			//
			if ( is_variable() )
				return 1;

			// For each operand, recurse and sum.
			//
			size_t out = 0;
			for ( auto& subexp : operands )
				out += subexp.depth();
			return out;
		}

		// Conversion to human readable format.
		//
		std::string to_string() const
		{
			// If variable redirect to it's own ::to_string.
			//
			if ( is_variable() )
				return value->to_string();
			fassert( fn );
			
			// If unary function:
			//
			if ( fn->is_unary )
			{
				fassert( operands.size() == 1 );
				return fn->symbol.size() 
					? fn->symbol + operands[ 0 ].to_string() 
					: fn->function + "(" + operands[ 0 ].to_string() + ")";
			}
			// If binary function:
			//
			fassert( operands.size() == 2 );
			return fn->symbol.size()
				? "(" + operands[ 0 ].to_string() + fn->symbol + operands[ 1 ].to_string() + ")"
				: fn->function + "(" + operands[ 0 ].to_string() + ", " + operands[ 1 ].to_string() + ")";
		}

		// Basic comparison operators.
		//
		bool operator==( const expression& o ) const 
		{
			if ( o.is_variable() )
				return is_variable() && value == o.value;
			else
				return is_expression() && fn == o.fn && operands == o.operands;
		}
		bool operator!=( const expression& o ) const { return !operator==( o ); }
		bool operator<( const expression& o ) const { return !operator==( o ) && to_string() < o.to_string(); }

		// Convinience wrapper for operand access.
		//
		auto operator[]( size_t i ) const { return operands[ i ]; }
		auto operator[]( size_t i ) { return operands[ i ]; }

		// Convinience wrappers around common operations.
		//
		expression operator+() const { return expression( *this ); }
		expression operator~() const { return expression( op( "not" ), *this ); }
		expression operator-() const { return expression( op( "neg" ), *this ); }
		expression operator+( const expression& b ) const { return expression( *this, op( "add" ), b ); }
		expression operator-( const expression& b ) const { return expression( *this, op( "sub" ), b ); }
		expression operator|( const expression& b ) const { return expression( *this, op( "or" ), b ); }
		expression operator&( const expression& b ) const { return expression( *this, op( "and" ), b ); }
		expression operator^( const expression& b ) const { return expression( *this, op( "xor" ), b ); }
		expression operator>>( const expression& b ) const { return expression( *this, op( "shr" ), b ); }
		expression operator<<( const expression& b ) const { return expression( *this, op( "shl" ), b ); }
		expression ror( const expression& b ) const { return expression( *this, op( "ror" ), b ); }
		expression rol( const expression& b ) const { return expression( *this, op( "ror" ), b ); }
	};
};