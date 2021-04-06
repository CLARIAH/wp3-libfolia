/*
  Copyright (c) 2006 - 2021
  CLST  - Radboud University
  ILK   - Tilburg University

  This file is part of libfolia

  libfolia is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  libfolia is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.

  For questions and suggestions, see:
      https://github.com/LanguageMachines/ticcutils/issues
  or send mail to:
      lamasoftware (at ) science.ru.nl
*/
/** @file folia_utils.cxx */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <stdexcept>
#include <algorithm>
#include "ticcutils/StringOps.h"
#include "ticcutils/XMLtools.h"
#include "ticcutils/PrettyPrint.h"
#include "ticcutils/Unicode.h"
#include "libfolia/folia.h"
#include "libfolia/folia_properties.h"

using namespace std;
using namespace icu;
using namespace TiCC;
using namespace icu;

namespace folia {

  FoliaElement *FoliaElement::createElement( const string& tag,
					     Document *doc ){
    /// create a new FoliaElement
    /*!
      \param tag the folia tagname of the desired element
      \param doc the Document the new element will be part of. May be 0
      \return a new FoliaElement
      throws if the tag is unknown
    */
    ElementType et = BASE;
    try {
      et = stringToElementType( tag );
    }
    catch ( const ValueError& e ){
      cerr << e.what() << endl;
      return 0;
    }
    FoliaElement *el = private_createElement( et );
    if ( doc ){
      el->assignDoc( doc );
    }
    return el;
  }

  FoliaElement *FoliaElement::createElement( ElementType et,
					     Document *doc ){
    /// create a new FoliaElement
    /*!
      \param et the ElementType the desired node
      \param doc the Document the new element will be part of. May be 0
      \return a new FoliaElement
    */
    FoliaElement *el = private_createElement( et );
    if ( doc ){
      el->assignDoc( doc );
    }
    return el;
  }

  KWargs::KWargs( const std::string& s ){
    /// create a KWargs from an input string
    /*!
      \param s The input string, in the following format:
      "att1='val1', att2='val2', ..., attn='valn'"
    */
    init( s );
  }

  void KWargs::init( const string& s ){
    /// initialize a KWargs from an input string
    /*!
      \param s The input string, in the following format:
      "att1='val1', att2='val2', ..., attn='valn'"
      all existing values are cleared
    */
    clear();
    bool quoted = false;
    bool parseatt = true;
    bool escaped = false;
    string att;
    string val;
    for ( const auto& let : s ){
      if ( let == '\\' ){
	if ( quoted ){
	  if ( escaped ){
	    val += let;
	    escaped = false;
	  }
	  else {
	    escaped = true;
	    continue;
	  }
	}
	else {
	  throw ArgsError( s + ", stray \\" );
	}
      }
      else if ( let == '\'' ){
	if ( quoted ){
	  if ( escaped ){
	    val += let;
	    escaped = false;
	  }
	  else {
	    if ( att.empty() || val.empty() ){
	      throw ArgsError( s + ", (''?)" );
	    }
	    (*this)[att] = val;
	    att.clear();
	    val.clear();
	    quoted = false;
	  }
	}
	else {
	  quoted = true;
	}
      }
      else if ( let == '=' ) {
	if ( parseatt ){
	  parseatt = false;
	}
	else if ( quoted ) {
	  val += let;
	}
	else {
	  throw ArgsError( s + ", stray '='?" );
	}
      }
      else if ( let == ',' ){
	if ( quoted ){
	  val += let;
	}
	else if ( !parseatt ){
	  parseatt = true;
	}
	else {
	  throw ArgsError( s + ", stray '='?" );
	}
      }
      else if ( let == ' ' ){
	if ( quoted )
	  val += let;
      }
      else if ( parseatt ){
	att += let;
      }
      else if ( quoted ){
	if ( escaped ){
	  val += "\\";
	  escaped = false;
	}
	val += let;
      }
      else {
	throw ArgsError( s + ", unquoted value or missing , ?" );
      }
    }
    if ( quoted )
      throw ArgsError( s + ", unbalanced '?" );
  }

  bool KWargs::is_present( const string& att ) const {
    /// check if an attribute is present in the KWargs
    /*!
      \param att The attribute to check
      \return true is present, otherwise false
    */
    return find(att) != end();
  }

  string KWargs::lookup( const string& att ){
    /// lookup an attribute
    /*!
      \param att The attribute to check
      \return the value if present, otherwise ""
    */
    string result;
    auto it = find(att);
    if ( it != end() ){
      result = it->second;
    }
    return result;
  }

  string KWargs::extract( const string& att ){
    /// lookup and removed an attribute
    /*!
      \param att The attribute to check
      \return the value if present, otherwise ""
      the attribute is cleared from the KWargs
    */
    string result;
    auto it = find(att);
    if ( it != end() ){
      result = it->second;
      erase(it);
    }
    return result;
  }

  KWargs getArgs( const string& s ){
  ///
  /// explicitely get a KWargs from a string
  /// \param s The input string, in the following format:
  /// "att1='val1', att2='val2', ..., attn='valn'"
  ///
    KWargs result( s );
    return result;
  }

  string toString( const KWargs& args ){
  ///
  /// Convert a KWargs to a string
  /// \param args the KWargs to convert
  /// returns a string, in the following format:
  /// "att1='val1', att2='val2', ..., attn='valn'"
  ///
    string result;
    auto it = args.begin();
    while ( it != args.end() ){
      result += it->first + "='" + it->second + "'";
      ++it;
      if ( it != args.end() )
	result += ",";
    }
    return result;
  }

  string KWargs::toString(){
    return folia::toString( *this );
  }

  KWargs getAttributes( const xmlNode *node ){
    /// extract a KWargs list from an xmlNode
    /*!
      \param node The xmlNode to examine
      \return a KWargs object with all attributes from 'node'
    */
    KWargs atts;
    if ( node ){
      xmlAttr *a = node->properties;
      while ( a ){
	if ( a->atype == XML_ATTRIBUTE_ID && string((char*)a->name) == "id" ){
	  atts["xml:id"] = (char *)a->children->content;
	}
	else if ( a->ns == 0 || a->ns->prefix == 0 ){
	  atts[(char*)a->name] = (char *)a->children->content;
	}
	else {
	  string pref = string( (const char*)a->ns->prefix);
	  string att  = string( (const char*)a->name);
	  if ( pref == "xlink" ){
	    atts["xlink:"+att] = (char *)a->children->content;
	  }
	  // else {
	  //   cerr << "attribute PREF=" << pref << endl;
	  // }
	}
	a = a->next;
      }
    }
    return atts;
  }

  void addAttributes( xmlNode *node, const KWargs& atts ){
    /// add all attributes from 'atts' as attribute nodes to 'node`
    /*!
      \param node The xmlNode to add to
      \param atts The list of attribute/value pairs
      some special care is taken for attributes 'xml:id', 'id' and 'lang'
    */
    KWargs attribs = atts;
    auto it = attribs.find("xml:id");
    if ( it != attribs.end() ){ // xml:id is special
      xmlSetProp( node, XML_XML_ID, (const xmlChar *)it->second.c_str() );
      attribs.erase(it);
    }
    it = attribs.find("lang");
    if ( it != attribs.end() ){ // lang is special too
      xmlNodeSetLang( node, (const xmlChar*)it->second.c_str() );
      attribs.erase(it);
    }
    it = attribs.find("id");
    if ( it != attribs.end() ){
      xmlSetProp( node, (const xmlChar*)("id"), (const xmlChar *)it->second.c_str() );
      attribs.erase(it);
    }
    // and now the rest
    it = attribs.begin();
    while ( it != attribs.end() ){
      xmlSetProp( node,
		  (const xmlChar*)it->first.c_str(),
		  (const xmlChar*)it->second.c_str() );
      ++it;
    }
  }

  int toMonth( const string& ms ){
    static const map<string,int> m_n_map
      = { { "jan", 0 },
	  { "feb", 1 },
	  { "mar", 2 },
	  { "apr", 3 },
	  { "may", 4 },
	  { "jun", 5 },
	  { "jul", 6 },
	  { "aug", 7 },
	  { "sep", 8 },
	  { "oct", 9 },
	  { "nov", 10 },
	  { "dec", 11 },
    };
    try {
      int result = stringTo<int>( ms );
      return result - 1;
    }
    catch( const exception& ){
      string m = TiCC::lowercase( ms );
      auto it = m_n_map.find( m );
      if ( it == m_n_map.end() ){
	throw runtime_error( "invalid month: " + m );
      }
      else {
	return it->second;
      }
    }
  }

  string parseDate( const string& s ){
    if ( s.empty() ){
      return "";
    }
    //    cerr << "try to read a date-time " << s << endl;
    vector<string> date_time;
    size_t num = TiCC::split_at( s, date_time, "T");
    if ( num == 0 ){
      num = TiCC::split_at( s, date_time, " ");
      if ( num == 0 ){
	cerr << "failed to read a date-time " << s << endl;
	return "";
      }
    }
    //    cerr << "found " << num << " parts" << endl;
    tm time = tm();
    if ( num == 1 || num == 2 ){
      //      cerr << "parse date " << date_time[0] << endl;
      vector<string> date_parts;
      size_t dnum = TiCC::split_at( date_time[0], date_parts, "-" );
      switch ( dnum ){
      case 3: {
	int mday = stringTo<int>( date_parts[2] );
	time.tm_mday = mday;
      }
	// fall through
      case 2: {
	int mon = toMonth( date_parts[1] );
	time.tm_mon = mon;
      }
	// fall through
      case 1: {
	int year = stringTo<int>( date_parts[0] );
	time.tm_year = year-1900;
      }
	break;
      default:
	cerr << "failed to read a date from " << date_time[0] << endl;
	return "";
      }
    }
    if ( num == 2 ){
      //      cerr << "parse time " << date_time[1] << endl;
      vector<string> date_parts;
      size_t dnum = TiCC::split_at( date_time[1], date_parts, ":" );
      //      cerr << "parts " << date_parts << endl;
      switch ( dnum ){
      case 4:
	// ignore
      case 3: {
	int sec = stringTo<int>( date_parts[2] );
	time.tm_sec = sec;
      }
	// fall through
      case 2: {
	int min = stringTo<int>( date_parts[1] );
	time.tm_min = min;
      }
	// fall through
      case 1: {
	int hour = stringTo<int>( date_parts[0] );
	time.tm_hour = hour;
      }
	break;
      default:
	cerr << "failed to read a time from " << date_time[1] << endl;
	return "";
      }
    }
    // cerr << "read _date time = " << toString(time) << endl;
    char buf[100];
    strftime( buf, 100, "%Y-%m-%dT%X", &time );
    return buf;
  }

  string parseTime( const string& s ){
    if ( s.empty() ){
      return "";
    }
    //    cerr << "try to read a time " << s << endl;
    vector<string> time_parts;
    tm time = tm();
    int num = TiCC::split_at( s, time_parts, ":" );
    if ( num != 3 ){
      cerr << "failed to read a time " << s << endl;
      return "";
    }
    time.tm_min = stringTo<int>( time_parts[1] );
    time.tm_hour = stringTo<int>( time_parts[0] );
    string secs = time_parts[2];
    num = TiCC::split_at( secs, time_parts, "." );
    time.tm_sec = stringTo<int>( time_parts[0] );
    string mil_sec = "000";
    if ( num == 2 ){
      mil_sec = time_parts[1];
    }
    char buf[100];
    strftime( buf, 100, "%X", &time );
    string result = buf;
    result += "." + mil_sec;
    //    cerr << "formatted time = " << result << endl;
    return result;
  }

  bool AT_sanity_check(){
    bool sane = true;
    size_t dim = s_ant_map.size();
    if ( dim != ant_s_map.size() ){
      cerr << "s_ant_map and ant_s_map are different in size!" << endl;
      return false;
    }
    for ( auto const& it : ant_s_map ){
      string s;
      try {
	s = toString( it.first );
      }
      catch (...){
	cerr << "no string translation for AnnotationType(" << int(it.first) << ")" << endl;
	sane = false;
      }
      if ( !s.empty() ){
	try {
	  stringTo<AnnotationType>( s );
	}
	catch ( const ValueError& e ){
	  cerr << "no AnnotationType found for string '" << s << "'" << endl;
	  sane = false;
	}
      }
    }
    return sane;
  }

  bool ET_sanity_check(){
    bool sane = true;
    for ( auto const& it : s_et_map ){
      ElementType et = it.second;
      if ( et_s_map.find(et) == et_s_map.end() ){
	cerr << "no et_s found for ElementType(" << int(et) << ")" << endl;
	return false;
      }
    }
    for ( auto const& it : et_s_map ){
      ElementType et = it.first;
      if ( s_et_map.find(it.second) == s_et_map.end() ){
	cerr << "no string found for ElementType(" << int(it.first) << ")" << endl;
	return false;
      }
      string s;
      try {
	s = toString( et );
      }
      catch (...){
	cerr << "toString failed for ElementType(" << int(et) << ")" << endl;
	sane = false;
      }
      if ( !s.empty() ){
	ElementType et2;
	try {
	  et2 = stringToElementType( s );
	}
	catch ( const ValueError& e ){
	  cerr << "no element type found for string '" << s << "'" << endl;
	  sane = false;
	  continue;
	}
	if ( et != et2 ){
	  cerr << "Argl: toString(ET) doesn't match original: " << s
	       << " vs " << toString(et2) << endl;
	  sane = false;
	  continue;
	}
	FoliaElement *tmp1 = 0;
	try {
	  tmp1 = AbstractElement::createElement( s );
	}
	catch( const ValueError &e ){
	  string err = e.what();
	  if ( err.find("abstract") ==string::npos ){
	    cerr << "createElement(" << s << ") failed! :" << err << endl;
	    sane = false;
	  }
	}
	if ( sane && tmp1 ){
	  FoliaElement *tmp2 = 0;
	  try {
	    tmp2 = AbstractElement::createElement( et );
	  }
	  catch( const ValueError &e ){
	    string err = e.what();
	    if ( err.find("abstract") == string::npos ){
	      cerr << "createElement(" << int(et) << ") failed! :" << err << endl;
	      sane = false;
	    }
	  }
	  if ( tmp2 != 0 ) {
	    if ( et != tmp2->element_id() ){
	      cerr << "the element type " << tmp2->element_id() << " of " << s
		   << " != " << et << " (" << toString(tmp2->element_id())
		   << " != " << toString(et) << ")" << endl;
	    sane = false;
	    }
	    if ( s != tmp2->xmltag() && s != "headfeature" ){
	      cerr << "the xmltag " << tmp2->xmltag() << " != " << s << endl;
	    sane = false;
	    }
	  }
	}
      }
    }
    return sane;
  }

  bool isNCName( const string& s ){
    /// test if a string is a valid NCName value
    /*!
      \param s the inputstring
      \return true if \e s may be used as an NCName (e.g. for xml:id)
    */
    int test = xmlValidateNCName( (const xmlChar*)s.c_str(), 0 );
    if ( test != 0 ){
      return false;
    }
    return true;
  }

  bool checkNS( const xmlNode *n, const string& ns ){
    string tns = TiCC::getNS(n);
    if ( tns == ns )
      return true;
    else
      throw runtime_error( "namespace conflict for tag:" + TiCC::Name(n)
			   + ", wanted:" + ns
			   + " got:" + tns );
    return false;
  }

  map<string,string> getNS_definitions( const xmlNode *node ){
    map<string,string> result;
    xmlNs *p = node->nsDef;
    while ( p ){
      string pre;
      string val;
      if ( p->prefix ){
	pre = (char *)p->prefix;
      }
      val = (char *)p->href;
      result[pre] = val;
      p = p->next;
    }
    return result;
  }

  string TextValue( const xmlNode *node ){
    /// extract the string content of an xmlNode
    /*!
      \param node The xmlNode to extract from
      \return the string value of node
    */
    string result;
    if ( node ){
      xmlChar *tmp = xmlNodeGetContent( node );
      if ( tmp ){
	result = string( (char *)tmp );
	xmlFree( tmp );
      }
    }
    return result;
  }

  icu::UnicodeString strip_control_chars( const icu::UnicodeString& input ){
    UnicodeString result;
    for ( int i=0; i < input.length(); ++i ){
       if (input[i] == 0x0a || input[i] == 0x09  || input[i] == 0x0d || input[i] == 0x85 || input[i] == 0x2028 || !u_iscntrl(input[i])) { //newline/cr and tab and a few others are okay, other control characters are ignored
          result += input[i];
       }
    }
    return result;
  }

  icu::UnicodeString normalize_spaces( const icu::UnicodeString& input,
				       bool replace_all_control_chars ){
    /// substitute NEWLINE, CARRIAGERETURN and TAB by spaces
    /// AND all multiple spaces by 1, also trims at back and front.
    /*!
      \param input the UnicodeString to normalize
      \param replace_all_control_chars when true, substitute all control-chars
      by a space too. (the default is true)
     */
    UnicodeString result;
    bool is_space = false;
    for ( int i=0; i < input.length(); ++i ){
      if ( u_isspace( input[i] ) ||  (replace_all_control_chars && u_iscntrl(input[i])) ){
	if ( is_space ){
	  continue;
	}
	is_space = true;
	result += " ";
      }
      else {
	is_space = false;
	result += input[i];
      }
    }
    result.trim(); // remove leading and trailing whitespace;
    return result;
  }

  bool is_norm_empty(const std::string& s) {
      /// checks if the string is empty after normalization (strips all control characters), strings with only spaces/newslines etc are considered empty too
      return normalize_spaces(TiCC::UnicodeFromUTF8(s)).isEmpty();
  }

  string get_ISO_date() {
    /// get the current date in ISO 8601 format
    time_t Time;
    time(&Time);
    tm curtime;
    localtime_r(&Time,&curtime);
    char buf[256];
    strftime( buf, 100, "%Y-%m-%dT%X", &curtime );
    string res = buf;
    return res;
  }

} //namespace folia
