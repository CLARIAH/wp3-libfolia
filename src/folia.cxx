#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <set>
#include <list>
#include <vector>
#include <map>
#include <algorithm>
#include <stdexcept>
#include "folia/document.h"
#include "folia/folia.h"
#include "config.h"

using namespace std;

std::ostream& operator<<( std::ostream& os, const AbstractElement& ae ){
  os << " <" << ae.classname();
  KWargs ats = ae.collectAttributes();
  if ( !ae._id.empty() )
    ats["id"] = ae._id;

  KWargs::const_iterator it = ats.begin();
  while ( it != ats.end() ){
    os << " " << it->first << "='" << it->second << "'";
    ++it;
  }
  os << " > {";
  for( size_t i=0; i < ae.data.size(); ++i ){
    os << "<" << ae.data[i]->classname() << ">,";
  }
  os << "}";
  return os;
}

std::ostream& operator<<( std::ostream&os, const AbstractElement *ae ){
  if ( !ae )
    os << "nil";
  else
    os << *ae;
  return os;
}

AbstractElement::AbstractElement( Document *d ){
  mydoc = d;
  _confidence = -1;
  _element_id = BASE;
  refcount = 0;
  _datetime = 0;
  _parent = 0;
  _required_attributes = NO_ATT;
  _optional_attributes = NO_ATT;
  _annotation_type = AnnotationType::NO_ANN;
  _annotator_type = UNDEFINED;
  _xmltag = "ThIsIsSoWrOnG";
  occurrences = 0;  //#Number of times this element may occur in its parent (0=unlimited, default=0)
  occurrences_per_set = 1; // #Number of times this element may occur per set (0=unlimited, default=1)
  MINTEXTCORRECTIONLEVEL = UNCORRECTED;
  TEXTDELIMITER = " " ;
  PRINTABLE = true;
}

AbstractElement::~AbstractElement( ){
  //  cerr << "delete element " << _xmltag << " *= " << (void*)this << endl;
  for ( size_t i=0; i < data.size(); ++i ){
    if ( data[i]->refcount == 0 ) // probably only for words
      delete data[i];
    else {
      mydoc->keepForDeletion( data[i] );
    }
  }
  delete _datetime;
}

void AbstractElement::setAttributes( const KWargs& kwargs ){
  Attrib supported = _required_attributes | _optional_attributes;
  // if ( _element_id == Correction_t ){
  //   cerr << "set attributes: " << kwargs << " on " << toString(_element_id) << endl;
  //   cerr << "required = " <<  _required_attributes << endl;
  //   cerr << "optional = " <<  _optional_attributes << endl;
  //   cerr << "supported = " << supported << endl;
  //   cerr << "ID & supported = " << (ID & supported) << endl;
  //   cerr << "ID & _required = " << (ID & _required_attributes ) << endl;
  // }
  if ( mydoc && mydoc->debug > 2 )
    cerr << "set attributes: " << kwargs << " on " << toString(_element_id) << endl;
  
  KWargs::const_iterator it = kwargs.find( "generate_id" );
  if ( it != kwargs.end() ) {
    if ( !mydoc ){
      throw runtime_error( "can't generate an ID without a doc" );
    }
    AbstractElement * e = (*mydoc)[it->second];
    if ( e ){
      _id = e->generateId( _xmltag );
    }
    else
      throw ValueError("Unable to genarate and if from ID= " + it->second );
  }
  else {
    it = kwargs.find( "id" );
    if ( it != kwargs.end() ) {
      if ( !ID & supported )
	throw ValueError("ID is not supported");
      else {
	_id = it->second;
      }
    }
    else if ( ID & _required_attributes )
      throw ValueError("ID is required for " + classname() );
    else
      _id = "";
  }

  it = kwargs.find( "set" );
  string def;
  if ( it != kwargs.end() ) {
    if ( !(CLASS & supported) )
      throw ValueError("Set is not supported");
    else {
      _set = it->second;
    }
    if ( mydoc &&
	 !mydoc->isDeclared( _set , _annotation_type ) )
      throw ValueError( "Set " + _set + " is used but has no declaration " +
			"for " + toString( _annotation_type ) + "-annotation" );
  }
  else if ( mydoc && ( def = mydoc->defaultset( _annotation_type )) != "" ){
    _set = def;
  }
  else if ( CLASS & _required_attributes )
    throw ValueError("Set is required for " + classname() );
  else
    _set = "";

  it = kwargs.find( "class" );
  if ( it == kwargs.end() )
    it = kwargs.find( "cls" );
  if ( it != kwargs.end() ) {
    if ( !( CLASS & supported ) )
      throw ValueError("Class is not supported for " + classname() );
    else {
      _cls = it->second;
    }
  }
  else if ( CLASS & _required_attributes )
    throw ValueError("Class is required for " + classname() );
  else
    _cls = "";

      
  it = kwargs.find( "annotator" );    
  if ( it != kwargs.end() ) {
    if ( !(ANNOTATOR & supported) )
      throw ValueError("Annotator is not supported for " + classname() );
    else {
      _annotator = it->second;
    }
  }
  else if ( mydoc &&
	    (def = mydoc->defaultannotator( _annotation_type, "", true )) != "" ){
    _annotator = def;
  }
  else if ( ANNOTATOR & _required_attributes )
    throw ValueError("Annotator is required for " + classname() );
  else
    _annotator = "";
  
  it = kwargs.find( "annotatortype" );    
  if ( it != kwargs.end() ) {
    if ( ! (ANNOTATOR & supported) )
      throw ValueError("Annotatortype is not supported for " + classname() );
    else {
      if ( it->second == "auto" )
	_annotator_type = AUTO;
      else if ( it->second == "manual" )
	_annotator_type = MANUAL;
      else
	throw ValueError("annotatortype must be 'auto' or 'manual'");
    }
  }
  else if ( mydoc &&
	    (def = mydoc->defaultannotatortype( _annotation_type, "", true ) ) != ""  ){
    if ( def == "auto" )
      _annotator_type = AUTO;
    else if ( def == "manual" )
      _annotator_type = MANUAL;
    else
      throw ValueError("annotatortype must be 'auto' or 'manual'");
  }
  else if ( ANNOTATOR & _required_attributes )
    throw ValueError("Annotatortype is required for " + classname() );
  else
    _annotator_type = UNDEFINED;

        
  it = kwargs.find( "confidence" );
  if ( it != kwargs.end() ) {
    if ( !(CONFIDENCE & supported) )
      throw ValueError("Confidence is not supported for " + classname() );
    else {
      _confidence = toDouble(it->second);
      if ( ( _confidence < 0.0 || _confidence > 1.0 ) )
	throw ValueError("Confidence must be a floating point number between 0 and 1");
    }
  }
  else if ( CONFIDENCE & _required_attributes )
    throw ValueError("Confidence is required for " + classname() );
  else
    _confidence = -1;

  it = kwargs.find( "n" );
  if ( it != kwargs.end() ) {
    if ( !(N & supported) )
      throw ValueError("N is not supported for " + classname() );
    else {
      _n = it->second;
    }
  }
  else if ( N & _required_attributes )
    throw ValueError("N is required");
  else
    _n = "";
  
  it = kwargs.find( "datetime" );
  if ( it != kwargs.end() ) {
    if ( !(DATETIME & supported) )
      throw ValueError("datetime is not supported for " + classname() );
    else {
      _datetime = parseDate( it->second );
      if ( _datetime == 0 )
	throw ValueError( "invalid datetime string:" + it->second );
    }
  }
  else if ( DATETIME & _required_attributes )
    throw ValueError("datetime is required");
  else
    _datetime = 0;

  if ( mydoc && !_id.empty() )
    mydoc->addDocIndex( this, _id );  
}

KWargs AbstractElement::collectAttributes() const {
  KWargs attribs;
  bool isDefaultSet = true;
  bool isDefaultAnn = true;
  if ( !_id.empty() ){
    attribs["_id"] = _id; // sort "id" as first!
  }
  if ( !_set.empty() &&
       _set != mydoc->defaultset( _annotation_type ) ){
    isDefaultSet = false;
    attribs["set"] = _set;
  }
  if ( !_cls.empty() )
    attribs["class"] = _cls;

  if ( !_annotator.empty() &&
       _annotator != mydoc->defaultannotator( _annotation_type, _set, true ) ){
    isDefaultAnn = false;
    attribs["annotator"] = _annotator;
  }
  
  if ( _annotator_type != UNDEFINED ){
    AnnotatorType at = stringToANT( mydoc->defaultannotatortype( _annotation_type, _set, true ) );
    if ( (!isDefaultSet || !isDefaultAnn) && _annotator_type != at ){
      if ( _annotator_type == AUTO )
	attribs["annotatortype"] = "auto";
      else if ( _annotator_type == MANUAL )
	attribs["annotatortype"] = "manual";
    }
  }
  
  if ( _confidence >= 0 )
    attribs["confidence"] = toString(_confidence);
  
  if ( !_n.empty() )
    attribs["n"] = _n;

  if ( _datetime != 0 )
    attribs["datetime"] = getDateTime();

  return attribs;
}

string AbstractElement::xmlstring() const{
  // serialize to a string (XML fragment)
  xmlNode *n = xml( true );
  xmlSetNs( n, xmlNewNs( n, (const xmlChar *)NSFOLIA.c_str(), 0 ) );
  xmlBuffer *buf = xmlBufferCreate();
  xmlNodeDump( buf, 0, n, 0, 0 );
  string result = (const char*)xmlBufferContent( buf );
  xmlBufferFree( buf );
  xmlFreeNode( n );
  return result;
}

xmlNode *AbstractElement::xml( bool recursive ) const {
  xmlNode *e = newXMLNode( foliaNs(), _xmltag );
  KWargs attribs = collectAttributes();
  addAttributes( e, attribs );
  if ( recursive ){
    // append children:
    vector<AbstractElement*>::const_iterator it=data.begin();
    while ( it != data.end() ){
      xmlAddChild( e, (*it)->xml( recursive ) );
      ++it;
    }
  }
  return e;
}

string AbstractElement::str() const {
  return _xmltag;
}

bool AbstractElement::hastext( TextCorrectionLevel corr ) const {
  //  cerr << _xmltag << "::hastext()" << endl;
  // Does this element have text?
  vector<AbstractElement*> v = select(TextContent_t,false);
  if ( corr == NOCORR ){
    // regardless of correctionlevel:
    return v.size() > 0;
  }
  else {
    vector<AbstractElement*>::const_iterator it = v.begin();
    while ( it != v.end() ){
      if ( (*it)->corrected() == corr )
	return true;
      ++it;
    }
  }
  return false;
}

UnicodeString AbstractElement::text( TextCorrectionLevel corr ) const {
  if ( !PRINTABLE )
    throw NoSuchText( _xmltag );
  //  cerr << "text() for " << _xmltag << " step 1 " << endl;
  if ( corr != NOCORR ){
    if ( MINTEXTCORRECTIONLEVEL > corr ){
      throw  NotImplementedError( "text() for " + _xmltag );
    }
    AbstractElement *t = 0;
    for( size_t i=0; i < data.size(); ++i ){
      if ( data[i]->element_id() == TextContent_t 
	   && data[i]->corrected() == corr ){
	t = data[i];
	break;
      }
    }
    //    cerr << "text() for " << _xmltag << " step 2 >> " << endl;
    if ( t ){
      return t->text();
    }
    else
      throw NoSuchText( "inside " + _xmltag );
  }
  else if ( hastext( CORRECTED )  ){
    //    cerr << "text() for " << _xmltag << " step 3 " << endl;
    return text( CORRECTED );
  }
  else {
    // try to get text from children.
    //    cerr << "text() for " << _xmltag << " step 4 " << endl;

    UnicodeString result;
    for( size_t i=0; i < data.size(); ++i ){
      try {
	UnicodeString tmp = data[i]->text();
	result += tmp;
	if ( !tmp.isEmpty() ){
	  result += UTF8ToUnicode( data[i]->getTextDelimiter() );
	}
      }
      catch ( NoSuchText& e ){
	//	cerr << "hmm: " << e.what() << endl;
      }
    }
    //    cerr << "text() for " << _xmltag << " step 5, result= " << result << endl;
    result.trim();
    if ( !result.isEmpty() ){
      //      cerr << "text() for " << _xmltag << " step 6, result= " << result << endl;
      return result;
    }
    else if ( MINTEXTCORRECTIONLEVEL <= UNCORRECTED ){
      //      cerr << "text() for " << _xmltag << " step 7"<< endl;
      return text( UNCORRECTED );
    }
    else
      throw NoSuchText( ":{" );
  }
}

vector<AbstractElement *>AbstractElement::findreplacables( AbstractElement *par ) const {
  return par->select( element_id(), _set, false );
}

void AbstractElement::replace( AbstractElement *child ){
  // Appends a child element like append(), but replaces any existing child 
  // element of the same type and set. 
  // If no such child element exists, this will act the same as append()
  
  vector<AbstractElement*> replace = child->findreplacables( this );
  if ( replace.empty() ){
    // nothing to replace, simply call append
    append( child );
  }
  else if ( replace.size() > 1 ){
    throw runtime_error( "Unable to replace. Multiple candidates found, unable to choose." );
  }
  else {
    remove( replace[0], true );
    append( child );
  }
}                

TextContent *AbstractElement::settext( const string& txt, 
				       TextCorrectionLevel lv ){
  TextCorrectionLevel myl = lv;
  if ( lv == NOCORR )
    myl = MINTEXTCORRECTIONLEVEL;
  KWargs args;
  args["value"] = txt;
  args["corrected"] = toString( lv );
  TextContent *node = new TextContent( mydoc );
  node->setAttributes( args );
  append( node );
  return node;
}

string AbstractElement::description() const {
  vector<AbstractElement *> v =  select( Description_t, false );
  if ( v.size() == 0 )
    throw NoDescription();
  else
    return v[0]->description();
}

bool AbstractElement::acceptable( ElementType t ) const {
  set<ElementType>::const_iterator it = _accepted_data.find( t );
  if ( it == _accepted_data.end() )
    return false;
  return true;
}
 
bool AbstractElement::addable( const AbstractElement *c,
			       const string& setname ) const {
  static set<ElementType> selectSet;
  if ( !acceptable( c->_element_id ) ){
    throw ValueError( "Unable to append object of type " + c->classname()
		      + " to a " + classname() );
    return false;
  }
  if ( c->occurrences > 0 ){
    vector<AbstractElement*> v = select( c->_element_id );
    size_t count = v.size();
    if ( count > c->occurrences )
      throw DuplicateAnnotationError( "Unable to add another object of type " + c->classname() + " to " + classname() + ". There are already " + toString(count) + " instances of this class, which is the maximum." );
    return false;
  }
  if ( c->occurrences_per_set > 0 && !setname.empty() &&
       ( CLASS & c->_required_attributes ) ){
    vector<AbstractElement*> v = select( c->_element_id, setname );
    size_t count = v.size();
    if ( count > c->occurrences_per_set )
      throw DuplicateAnnotationError( "Unable to add another object of type " + c->classname() + " to " + classname() + ". There are already " + toString(count) + " instances of this class, which is the maximum." );
    return false;
  }
  return true;
}
 
AbstractElement *AbstractElement::append( AbstractElement *child ){
  bool ok = false;
  try {
    ok = addable( child );
  }
  catch ( exception& ){
    delete child;
    throw;
  }
  if ( ok ){
    data.push_back(child);
    if ( !child->mydoc ){
      child->mydoc = mydoc;
      string id = child->id();
      if ( !id.empty() )
	mydoc->addDocIndex( child, id );  
    }
    if ( !child->_parent ) // Only for WordRef i hope
      child->_parent = this;
    return child->postappend();
  }
  return 0;
}


void AbstractElement::remove( AbstractElement *child, bool del ){
  vector<AbstractElement*>::iterator it = std::remove( data.begin(), data.end(), child );
  data.erase( it, data.end() );
  if ( del )
    delete child;
}

AbstractElement* AbstractElement::index( size_t i ) const {
  if ( i < data.size() )
    return data[i];
  else
    throw range_error( "[] index out of range" );
}

AbstractElement* AbstractElement::rindex( size_t i ) const {
  if ( i < data.size() )
    return data[data.size()-1-i];
  else
    throw range_error( "[] rindex out of range" );
}

vector<AbstractElement*> AbstractElement::words() const {
  return select( Word_t );
}

AbstractElement* AbstractElement::words( size_t index ) const {
  vector<AbstractElement*> v = words();
  if ( index < v.size() ){
    return v[index];
  }
  else
    throw range_error( "word index out of range" );
}

AbstractElement* AbstractElement::rwords( size_t index ) const {
  vector<AbstractElement*> v = words();
  if ( index < v.size() ){
    return v[v.size()-1-index];
  }
  else
    throw range_error( "word reverse index out of range" );
}

vector<AbstractElement *>AbstractElement::annotations( ElementType et ) const {
  vector<AbstractElement *>v = select( et );
  if ( v.size() >= 1 )
    return v;
  else
    throw NoSuchAnnotation( toString(et) );
}

vector<AbstractElement*> AbstractElement::select( ElementType et,
						  const string& val,
						  const set<ElementType>& exclude,
						  bool recurse ) const {
  vector<AbstractElement*> res;
  for ( size_t i = 0; i < data.size(); ++i ){
    if ( data[i]->_element_id == et  && data[i]->_set == val ){
      res.push_back( data[i] );
    }
    if ( recurse ){
      if ( exclude.find( data[i]->_element_id ) == exclude.end() ){
	vector<AbstractElement*> tmp = data[i]->select( et, val, exclude, recurse );
	res.insert( res.end(), tmp.begin(), tmp.end() );
      }
    }
  }
  return res;
}

vector<AbstractElement*> AbstractElement::select( ElementType et,
						  const string& val,
						  bool recurse ) const {
  static set<ElementType> excludeSet;
  if ( excludeSet.empty() ){
    excludeSet.insert( Original_t );
    excludeSet.insert( Suggestion_t );
    excludeSet.insert( Alternative_t );
  }
  return select( et, val, excludeSet, recurse );
}

vector<AbstractElement*> AbstractElement::select( ElementType et,
						  const set<ElementType>& exclude,
						  bool recurse ) const {
  vector<AbstractElement*> res;
  for ( size_t i = 0; i < data.size(); ++i ){
    if ( data[i]->_element_id == et ){
      res.push_back( data[i] );
    }
    if ( recurse ){
      if ( exclude.find( data[i]->_element_id ) == exclude.end() ){
	vector<AbstractElement*> tmp = data[i]->select( et, exclude, recurse );
	res.insert( res.end(), tmp.begin(), tmp.end() );
      }
    }
  }
  return res;
}

vector<AbstractElement*> AbstractElement::select( ElementType et,
						  bool recurse ) const {
  static set<ElementType> excludeSet;
  if ( excludeSet.empty() ){
    excludeSet.insert( Original_t );
    excludeSet.insert( Suggestion_t );
    excludeSet.insert( Alternative_t );
  }
  return select( et, excludeSet, recurse );
}

AbstractElement* AbstractElement::parseXml( const xmlNode *node ){
  KWargs att = getAttributes( node );
  setAttributes( att );
  xmlNode *p = node->children;
  while ( p ){
    if ( p->type == XML_ELEMENT_NODE ){
      string tag = Name( p );
      AbstractElement *t = createElement( mydoc, tag );
      if ( t ){
	if ( mydoc->debug > 2 )
	  cerr << "created " << t << endl;
	t = t->parseXml( p );
	if ( t ){
	  if ( mydoc->debug > 2 )
	    cerr << "extend " << this << " met " << tag << endl;
	  append( t );
	}
      }
    }
    p = p->next;
  }
  return this;
}

void AbstractElement::setDateTime( const std::string& s ){
  Attrib supported = _required_attributes | _optional_attributes;
  if ( !(DATETIME & supported) )
    throw ValueError("datetime is not supported for " + classname() );
  else {
    _datetime = parseDate( s );
    if ( _datetime == 0 )
      throw ValueError( "invalid datetime string:" + s );
  }
}

string AbstractElement::getDateTime() const {
  char buf[100];
  strftime( buf, 100, "%Y-%m-%dT%X", _datetime );
  return buf;
}

Alternative *AbstractElement::addAlternative( ElementType et,
					      const KWargs& args ){
  Alternative *res = new Alternative( mydoc );
  KWargs kw;
  string id = generateId( "alt" );
  kw["id"] = id;
  res->setAttributes( kw );
  if ( et == Pos_t )
    res->addPosAnnotation( args );
  else if ( et == Lemma_t )
    res->addLemmaAnnotation( args );
  else
    throw runtime_error( "addAlternative not implemenentd for " + toString(et) );
  append( res );
  return res;
}

AbstractTokenAnnotation *AbstractElement::addAnnotation( ElementType et,
							 const KWargs& args ){
  if ( et == Pos_t )
    return addPosAnnotation( args );
  else if ( et == Lemma_t )
    return addLemmaAnnotation( args );
  else
    throw runtime_error( "addAnnotation not implemenentd for " + toString(et) );
}

AbstractTokenAnnotation *AbstractElement::addPosAnnotation( const KWargs& args ){
  PosAnnotation *res = new PosAnnotation( mydoc );
  try {
    res->setAttributes( args );
  }
  catch( exception& ){
    delete res;
    throw;
  }
  append( res );
  return res;
}

AbstractTokenAnnotation *AbstractElement::addLemmaAnnotation( const KWargs& args ){
  LemmaAnnotation *res = new LemmaAnnotation( mydoc );
  try {
    res->setAttributes( args );
  }
  catch( exception& ){
    delete res;
    throw;
  }
  append( res );
  return res;
}

Sentence *AbstractElement::addSentence( const KWargs& args ){
  Sentence *res = new Sentence( mydoc );
  KWargs kw = args;
  if ( kw["id"].empty() ){
    string id = generateId( "s" );
    kw["id"] = id;
  }
  try {
    res->setAttributes( kw );
  }
  catch( DuplicateIDError& e ){
    delete res;
    throw e;
  }
  append( res );
  return res;
}

Word *AbstractElement::addWord( const KWargs& args ){
  Word *res = new Word( mydoc );
  KWargs kw = args;
  if ( kw["id"].empty() ){
    string id = generateId( "w" );
    kw["id"] = id;
  }
  try {
    res->setAttributes( kw );
  }
  catch( DuplicateIDError& e ){
    delete res;
    throw e;
  }
  append( res );
  return res;
}

Correction *Sentence::splitWord( AbstractElement *orig, AbstractElement *p1, AbstractElement *p2, const KWargs& args ){
  vector<AbstractElement*> ov;
  ov.push_back( orig );
  vector<AbstractElement*> nv;
  nv.push_back( p1 );
  nv.push_back( p2 );
  vector<AbstractElement*> nil;
  return correctWords( ov, nv, nil, args );
}

Correction *Sentence::mergewords( AbstractElement *nw, 
				  const vector<AbstractElement *>& orig,
				  const string& args ){
  vector<AbstractElement*> nv;
  nv.push_back( nw );
  vector<AbstractElement*> nil;
  return correctWords( orig, nv, nil, getArgs(args) );
}

Correction *Sentence::deleteword( AbstractElement *w, 
				  const string& args ){
  vector<AbstractElement*> ov;
  ov.push_back( w );
  vector<AbstractElement*> nil;
  return correctWords( ov, nil, nil, getArgs(args) );
}

Correction *Sentence::insertword( AbstractElement *w, 
				  AbstractElement *p,
				  const string& args ){
  if ( !p || !p->isinstance( Word_t ) )
    throw runtime_error( "insertword(): previous is not a Word " );
  if ( !w || !w->isinstance( Word_t ) )
    throw runtime_error( "insertword(): new word is not a Word " );
  Word *tmp = new Word( "text='dummy', id='dummy'" );
  tmp->setParent( this ); // we create a dummy Word as member of the
                          // Sentence. This makes correct() happy
  vector<AbstractElement *>::iterator it = data.begin();
  while ( it != data.end() ){
    if ( *it == p ){
      it = data.insert( ++it, tmp );
      break;
    }
    ++it;
  }
  if ( it == data.end() )
    throw runtime_error( "insertword(): previous not found" );
  vector<AbstractElement *> ov;
  ov.push_back( *it );
  vector<AbstractElement *> nv;
  nv.push_back( w );
  vector<AbstractElement*> nil;
  return correctWords( ov, nv, nil, getArgs(args) );  
}

Correction *Sentence::correctWords( const vector<AbstractElement *>& orig,
				    const vector<AbstractElement *>& _new,
				    const vector<AbstractElement *>& current, 
				    const KWargs& args ){
  // Generic correction method for words. You most likely want to use the helper functions
  //      splitword() , mergewords(), deleteword(), insertword() instead
  
  // sanity check:
  vector<AbstractElement *>::const_iterator it = orig.begin();
  while ( it != orig.end() ){
    if ( !(*it) || !(*it)->isinstance( Word_t) )
      throw runtime_error("Original word is not a Word instance" );
    else if ( (*it)->sentence() != this )
      throw runtime_error( "Original not found as member of sentence!");
    ++it;
  }
  it = _new.begin();
  while ( it != _new.end() ){
    if ( ! (*it)->isinstance( Word_t) )
      throw runtime_error("new word is not a Word instance" );
    ++it;
  }
  it = current.begin();
  while ( it != current.end() ){
    if ( ! (*it)->isinstance( Word_t) )
      throw runtime_error("current word is not a Word instance" );
    ++it;
  }
  KWargs::const_iterator ait = args.find("suggest");
  if ( ait != args.end() && ait->second == "true" ){
    vector<AbstractElement *> nil;
    return correct( nil, orig, nil, _new, args );
  }
  else {
    vector<AbstractElement *> nil;
    return correct( orig, nil, _new, nil, args );
  }
}


void TextContent::setAttributes( const KWargs& args ){
  KWargs kwargs = args; // need to copy
  KWargs::const_iterator it = kwargs.find( "value" );
  if ( it != kwargs.end() ) {
    _text = UTF8ToUnicode(it->second);
    kwargs.erase("value");
  }
   else
     throw ValueError("TextContent expects value= parameter");
  it = kwargs.find( "corrected" );
  if ( it != kwargs.end() ) {
    _corrected = stringToTCL(it->second);
    kwargs.erase("corrected");
  }
  it = kwargs.find( "offset" );
  if ( it != kwargs.end() ) {
    _offset = stringTo<int>(it->second);
    kwargs.erase("offset");
  }
  else
    _offset = -1;
  it = kwargs.find( "newoffset" );
  if ( it != kwargs.end() ) {
    _newoffset = stringTo<int>(it->second);
    kwargs.erase("newoffset");
  }
  else
    _newoffset = -1;
  it = kwargs.find( "ref" );
  if ( it != kwargs.end() ) {
    throw NotImplementedError( "ref attribute in TextContent" );
  }
  it = kwargs.find( "length" );
  if ( it != kwargs.end() ) {
    _length = stringTo<int>(it->second);
    kwargs.erase("length");
  }
  else
    _length = _text.length();

  AbstractElement::setAttributes(kwargs);
}

AbstractElement* TextContent::parseXml( const xmlNode *node ){
  KWargs att = getAttributes( node );
  att["value"] = XmlContent( node );
  setAttributes( att );
  if ( mydoc->debug > 2 )
    cerr << "set textcontent to " << _text << endl;
  return this;
}

AbstractElement *TextContent::postappend(){
  TextCorrectionLevel pl = _parent->getMinCorrectionLevel();
  if ( _corrected == NOCORR ){
    _corrected = pl;
  }
  if ( _corrected < pl ) {
    throw ValueError( "TextContent(" + toString( _corrected ) + ") must be of higher level than its parents minimum (" + toString(pl) + ")" );
  }
  if ( _corrected == UNCORRECTED || _corrected == CORRECTED ){
    // sanity check, there may be no other TextContent child with the same 
    // correction level
    for ( size_t i=0; i < _parent->size(); ++i ){
      AbstractElement *child = _parent->index(i);
      if ( child != this && child->element_id() == TextContent_t &&
	   child->corrected() == _corrected ){
	throw DuplicateAnnotationError( "A TextContent with 'corrected' value of " +  toString(_corrected) + " already exists." );
      }
    }
  }
  // no conflict found
  return this;
}

vector<AbstractElement *>TextContent::findreplacables( AbstractElement *par ){
  vector<AbstractElement*> v = par->select( TextContent_t, _set, false );
  // cerr << "TextContent::findreplacable found " << v << endl;
  // cerr << "looking for " << toString( _corrected) << endl;
  vector<AbstractElement *>::iterator it = v.begin();
  while ( it != v.end() ){
    // cerr << "TextContent::findreplacable bekijkt " << *it << " (" 
    // 	 << toString( dynamic_cast<TextContent*>(*it)->_corrected ) << ")" << endl;
    if ( dynamic_cast<TextContent*>(*it)->_corrected != _corrected )
      it = v.erase(it);
    else
      ++it;
  }
  //  cerr << "TextContent::findreplacable resultaat " << v << endl;
  return v;
}


string TextContent::str() const{
  return UnicodeToUTF8(_text);
}

UnicodeString TextContent::text( TextCorrectionLevel l ) const{
  //  cerr << "TextContent::text()  step 1 " << endl;
  if ( l == NOCORR || l == _corrected )
    return _text;
  else
    throw NoSuchText( _xmltag );
}

string AbstractStructureElement::generateId( const string& tag, 
					     const string& id_in  ){
  //  cerr << "generateId," << _xmltag << " maxids=" << maxid << endl;
  int max = getMaxId(tag);
  //  cerr << "MAX = " << max << endl;
  string id = id_in;
  if ( id.empty() )
    id = _id;
  id += '.' + tag + '.' +  toString( max + 1 );
  return id;
}

string AbstractStructureElement::str() const{
  UnicodeString result = text();
  return UnicodeToUTF8(result);
}
  
void AbstractStructureElement::setMaxId( AbstractElement *child ) {
  if ( !child->id().empty() && !child->xmltag().empty() ){
    vector<string> parts;
    size_t num = split_at( child->id(), parts, "." );
    if ( num > 0 ){
      string val = parts[num-1];
      int i = stringTo<int>( val );
      map<string,int>::iterator it = maxid.find( child->xmltag() );
      if ( it == maxid.end() ){
	maxid[child->xmltag()] = i;
      }
      else {
	if ( it->second < i ){
	  it->second = i;
	}
      }
    }
  }
}

int AbstractStructureElement::getMaxId( const string& xmltag ) {
  int res = 0;
  if ( !xmltag.empty() ){
    res = maxid[xmltag];
    ++maxid[xmltag];
  }
  return res;
}

AbstractElement *AbstractStructureElement::append( AbstractElement *child ){
  AbstractElement::append( child );
  setMaxId( child );
  return child;
}

Correction *AbstractStructureElement::correct( vector<AbstractElement*> original,
					       vector<AbstractElement*> current,
					       vector<AbstractElement*> _new,
					       vector<AbstractElement*> suggestions,
					       const KWargs& args_in ){
  // cerr << "correct " << this << endl;
  // cerr << "original= " << original << endl;
  // cerr << "current = " << current << endl;
  // cerr << "new     = " << _new << endl;
  // cerr << "suggestions     = " << suggestions << endl;
  //  cerr << "args in     = " << args_in << endl;
  // Apply a correction
  Correction *c = 0;
  bool suggestionsonly = false;
  bool hooked = false;
  AbstractElement * addnew = 0;
  KWargs args = args_in;
  KWargs::const_iterator it = args.find("new");
  if ( it != args.end() ){
    TextContent *t = new TextContent( mydoc, "value='" +  it->second + "'" );
    _new.push_back( t );
    args.erase("new");
  }
  it = args.find("suggestion");
  if ( it != args.end() ){
    TextContent *t = new TextContent( mydoc, "value='" +  it->second + "'" );
    suggestions.push_back( t );
    args.erase("suggestion");
  }
  it = args.find("reuse");
  if ( it != args.end() ){
    // reuse an existing correction instead of making a new one
    try {
      c = dynamic_cast<Correction*>(mydoc->index(it->second)); 
    }
    catch ( exception& e ){
      throw ValueError("reuse= must point to an existing correction id!");
    }
    if ( !c->isinstance( Correction_t ) ){
      throw ValueError("reuse= must point to an existing correction id!");
    }
    hooked = true;
    suggestionsonly = (!c->hasNew() && !c->hasOriginal() && c->hasSuggestions() );
    if ( !_new.empty() && c->hasCurrent() ){
      // can't add new if there's current, so first set original to current, and then delete current
      
      if ( !current.empty() )
	throw runtime_error( "Can't set both new= and current= !");
      if ( original.empty() ){
	AbstractElement *cur = c->getCurrent();
	original.push_back( cur );
	c->remove( cur, false );
      }
    }
  }
  else {
    KWargs args2 = args;
    args2.erase("suggestion" );
    args2.erase("suggestions" );
    string id = generateId( "correction" );
    args2["id"] = id;
    c = new Correction(mydoc );
    c->setAttributes( args2 );
  }
  
  if ( !current.empty() ){
    if ( !original.empty() || !_new.empty() )
      throw runtime_error("When setting current=, original= and new= can not be set!");
    vector<AbstractElement *>::iterator cit = current.begin();
    while ( cit != current.end() ){
      AbstractElement *add = new Current( mydoc );
      add->append( *cit );
      c->replace( add );
      if ( !hooked ) {
	for ( size_t i=0; i < data.size(); ++i ){
	  if ( data[i] == *cit ){
	    //	    delete data[i];
	    data[i] = c;
	    hooked = true;
	  }
	}
      }
      ++cit;
    }
  }
  if ( !_new.empty() ){
    //    cerr << "there is new! " << endl;
    addnew = new NewElement( mydoc );
    c->append(addnew);
    vector<AbstractElement *>::iterator nit = _new.begin();    
    while ( nit != _new.end() ){
      addnew->append( *nit );
      ++nit;
    }
    //    cerr << "after adding " << c << endl;
    vector<AbstractElement*> v = c->select(Current_t);
    //delete current if present
    nit = v.begin();        
    while ( nit != v.end() ){
      c->remove( *nit, false );
      ++nit;
    }
  }
  if ( !original.empty() ){
    AbstractElement *add = new Original( mydoc );
    c->replace(add);
    vector<AbstractElement *>::iterator nit = original.begin();
    while ( nit != original.end() ){
      bool dummyNode = ( (*nit)->id() == "dummy" );
      if ( !dummyNode )
	add->append( *nit );
      for ( size_t i=0; i < data.size(); ++i ){
	if ( data[i] == *nit ){
	  if ( !hooked ) {
	    if ( dummyNode )
	      delete data[i];
	    data[i] = c;
	    hooked = true;
	  }
	  else 
	    remove( *nit, false );
	}
      }
      ++nit;
    }
  }
  else if ( addnew ){
    // original not specified, find automagically:
    vector<AbstractElement *> orig;
    //    cerr << "start to look for original " << endl;
    for ( size_t i=0; i < len(addnew); ++ i ){
      AbstractElement *p = addnew->index(i);
      //      cerr << "bekijk " << p << endl;
      vector<AbstractElement*> v = p->findreplacables( this );
      vector<AbstractElement*>::iterator vit=v.begin();      
      while ( vit != v.end() ){
	orig.push_back( *vit );
	++vit;
      }
    }
    if ( orig.empty() ){
      throw runtime_error( "No original= specified and unable to automatically infer");
    }
    else {
      //      cerr << "we seem to have some originals! " << endl;
      AbstractElement *add = new Original( mydoc );
      c->replace(add);
      vector<AbstractElement *>::iterator oit = orig.begin();
      while ( oit != orig.end() ){
	//	cerr << " an original is : " << *oit << endl;
	add->append( *oit );
	for ( size_t i=0; i < data.size(); ++i ){
	  if ( data[i] == *oit ){
	    if ( !hooked ) {
	      //delete data[i];
	      data[i] = c;
	      hooked = true;
	    }
	    else 
	      remove( *oit, false );
	  }
	}
	++oit;
      }
      vector<AbstractElement*> v = c->select(Current_t);
      //delete current if present
      oit = v.begin();        
      while ( oit != v.end() ){
	remove( *oit, false );
	++oit;
      }
    }
  }
  
  if ( addnew ){
    vector<AbstractElement*>::iterator oit = original.begin();
    while ( oit != original.end() ){
      c->remove( *oit, false );
      ++oit;
    }
  }

  if ( !suggestions.empty() ){
    AbstractElement *add = new Suggestion( mydoc );
    c->append(add);
    if ( !hooked )
      append(c);
    vector<AbstractElement *>::iterator nit = suggestions.begin();
    while ( nit != suggestions.end() ){
      add->append( *nit );
      ++nit;
    }
  }
  
  it = args.find("reuse");
  if ( it != args.end() ){
    if ( addnew && suggestionsonly ){
      vector<AbstractElement *> sv = c->suggestions();
      for ( size_t i=0; i < sv.size(); ++i ){
	if ( !c->annotator().empty() && sv[i]->annotator().empty() )
	  sv[i]->annotator( c->annotator() );
	if ( !(c->annotatortype() == UNDEFINED) && 
	     (sv[i]->annotatortype() == UNDEFINED ) )
	  sv[i]->annotatortype( c->annotatortype() );
      }
    }
    it = args.find("annotator");
    if ( it != args.end() )
      c->annotator( it->second );
    it = args.find("annotatortype");
    if ( it != args.end() )
      c->annotatortype( stringToANT(it->second) );
    it = args.find("confidence");
    if ( it != args.end() )
      c->confidence( stringTo<double>(it->second) );

  }

  return c;
}

const AbstractElement* AbstractStructureElement::resolveword( const string& id ) const{
  const AbstractElement *result = 0;
  for ( size_t i=0; i < data.size(); ++i ){
    result = data[i]->resolveword( id );
    if ( result )
      return result;
  }
  return result;
};

AbstractElement *AbstractStructureElement::annotation( ElementType et ) const {
  vector<AbstractElement *>v = annotations( et );
  return v[0]; // always exist, otherwise annotations would throw()
}

AbstractElement *AbstractStructureElement::annotation( ElementType et,
						       const string& val ) const {
  // Will return a SINGLE annotation (even if there are multiple). 
  // Raises a NoSuchAnnotation exception if none was found
  vector<AbstractElement *>v = select( et, val );
  if ( v.size() >= 1 )
    return v[0];
  else
    throw NoSuchAnnotation( toString(et) );
  return 0;
}

vector<AbstractElement *> AbstractStructureElement::alternatives( const string& set,
								  AnnotationType::AnnotationType type ) const{
  // Return a list of alternatives, either all or only of a specific type, restrained by set
  vector<AbstractElement*> alts = select( Alternative_t );
  if ( type == AnnotationType::NO_ANN ){
    return alts;
  }
  else {
    vector<AbstractElement*> res;
    for ( size_t i=0; i < alts.size(); ++i ){
      if ( alts[i]->size() > 0 ) { // child elements?
	for ( size_t j =0; j < alts[i]->size(); ++j ){
	  if ( alts[i]->index(j)->annotation_type() == type &&
	       ( alts[i]->st().empty() || alts[i]->st() == set ) ){
	    res.push_back( alts[i] ); // not the child!
	    break; // yield an alternative only once (in case there are multiple matches)
	  }
	}
      }
    }
    return res;
  }
};

void Word::setAttributes( const KWargs& args ){
  KWargs::const_iterator it = args.find( "space" );
  if ( it != args.end() ){
    if ( it->second == "no" ){
      space = false;
    }
  }
  it = args.find( "text" );
  if ( it != args.end() ) {
    settext( it->second );
  }
  it = args.find( "correctedtext" );
  if ( it != args.end() ) {
    settext( it->second, CORRECTED );
  }
  it = args.find( "uncorrectedtext" );
  if ( it != args.end() ) {
    settext( it->second, UNCORRECTED );
  }
  AbstractElement::setAttributes( args );
}

KWargs Word::collectAttributes() const {
  KWargs atts = AbstractElement::collectAttributes();
  if ( !space ){
    atts["space"] = "no";
  }
  return atts;
}

Correction *Word::correct( const std::string& s ){
  vector<AbstractElement*> nil;
  KWargs args = getArgs( s );
  //  cerr << "word::correct() <== " << this << endl;
  Correction *tmp = AbstractStructureElement::correct( nil, nil, nil, nil, args );
  //  cerr << "word::correct() ==> " << this << endl;
  return tmp;
}

Correction *Word::correct( AbstractElement *old,
			   AbstractElement *_new,
			   const KWargs& args ){
  vector<AbstractElement *> nv;
  nv.push_back( _new );
  vector<AbstractElement *> ov;
  ov.push_back( old );
  vector<AbstractElement *> nil;
  //  cerr << "correct() <== " << this;
  Correction *tmp =AbstractStructureElement::correct( ov, nil, nv, nil, args );
  //  cerr << "correct() ==> " << this;
  return tmp;
}

AbstractElement *Word::split( AbstractElement *part1, AbstractElement *part2,
			      const std::string& args ){
  return sentence()->splitWord( this, part1, part2, getArgs(args) );
}

AbstractElement *Word::append( AbstractElement *child ) {
  if ( child->element_id() == Pos_t ||
       child->element_id() == Lemma_t ){
    // sanity check, there may be no other child within the same set
    try {
      annotation( child->element_id(), child->st() );
    }
    catch ( NoSuchAnnotation &e ){
      // OK!
      return AbstractElement::append( child );
    }
    delete child;
    throw DuplicateAnnotationError( "Word::append" );
  }
  return AbstractElement::append( child );
}

Sentence *Word::sentence( ) const {
  // return the sentence this word is a part of, otherwise return null
  AbstractElement *p = _parent; 
  while( p ){
    if ( p->isinstance( Sentence_t ) )
      return dynamic_cast<Sentence*>(p);
    p = p->parent();
  }
  return 0;
}

AbstractElement *Word::previous() const{
  Sentence *s = sentence();
  vector<AbstractElement*> words = s->words();
  for( size_t i=0; i < words.size(); ++i ){
    if ( words[i] == this ){
      if ( i > 0 )
	return words[i-1];
      else 
	return 0;
      break;	
    }
  }
  return 0;
}

AbstractElement *Word::next() const{
  Sentence *s = sentence();
  vector<AbstractElement*> words = s->words();
  for( size_t i=0; i < words.size(); ++i ){
    if ( words[i] == this ){
      if ( i+1 < words.size() )
	return words[i+1];
      else 
	return 0;
      break;	
    }
  }
  return 0;
}

vector<AbstractElement *> Word::context( size_t size, 
					 const string& val ) const {
  vector<AbstractElement *> result;
  if ( size > 0 ){
    vector<AbstractElement*> words = mydoc->words();
    for( size_t i=0; i < words.size(); ++i ){
      if ( words[i] == this ){
	size_t miss = 0;
	if ( i < size ){
	  miss = size - i;
	}
	for ( size_t index=0; index < miss; ++index ){
	  if ( val.empty() )
	    result.push_back( 0 );
	  else {
	    PlaceHolder *p = new PlaceHolder( val );
	    mydoc->keepForDeletion( p );
	    result.push_back( p );
	  }
	}
	for ( size_t index=i-size+miss; index < i + size + 1; ++index ){
	  if ( index < words.size() ){
	    result.push_back( words[index] );
	  }
	  else {
	    if ( val.empty() )
	      result.push_back( 0 );
	    else {
	      PlaceHolder *p = new PlaceHolder( val );
	      mydoc->keepForDeletion( p );
	      result.push_back( p );
	    }
	  }
	}
	break;
      }
    }
  }
  return result;
}


vector<AbstractElement *> Word::leftcontext( size_t size, 
					     const string& val ) const {
  //  cerr << "leftcontext : " << size << endl;
  vector<AbstractElement *> result;
  if ( size > 0 ){
    vector<AbstractElement*> words = mydoc->words();
    for( size_t i=0; i < words.size(); ++i ){
      if ( words[i] == this ){
	size_t miss = 0;
	if ( i < size ){
	  miss = size - i;
	}
	for ( size_t index=0; index < miss; ++index ){
	  if ( val.empty() )
	    result.push_back( 0 );
	  else {
	    PlaceHolder *p = new PlaceHolder( val );
	    mydoc->keepForDeletion( p );
	    result.push_back( p );
	  }
	}
	for ( size_t index=i-size+miss; index < i; ++index ){
	  result.push_back( words[index] );
	}
	break;
      }
    }
  }
  return result;
}

vector<AbstractElement *> Word::rightcontext( size_t size, 
					      const string& val ) const {
  vector<AbstractElement *> result;
  //  cerr << "rightcontext : " << size << endl;
  if ( size > 0 ){
    vector<AbstractElement*> words = mydoc->words();
    size_t begin;
    size_t end;
    for( size_t i=0; i < words.size(); ++i ){
      if ( words[i] == this ){
	begin = i + 1;
	end = begin + size;
	for ( ; begin < end; ++begin ){
	  if ( begin >= words.size() ){
	    if ( val.empty() )
	      result.push_back( 0 );
	    else {
	      PlaceHolder *p = new PlaceHolder( val );
	      mydoc->keepForDeletion( p );
	      result.push_back( p );
	    }
	  }
	  else
	    result.push_back( words[begin] );
	}
	break;
      }
    }
  }
  return result;
}

const AbstractElement* Word::resolveword( const string& id ) const{
  if ( _id == id )
    return this;
  return 0;
};

AbstractElement* WordReference::parseXml( const xmlNode *node ){
  KWargs att = getAttributes( node );
  string id = att["id"];
  if ( id.empty() )
    throw XmlError( "empty id in WordReference" );
  if ( mydoc->debug ) 
    cerr << "Found word reference" << id << endl;
  AbstractElement *res = (*mydoc)[id];
  if ( res ){
    res->increfcount();
  }
  else {
    if ( mydoc->debug )
      cerr << "...Unresolvable!" << endl;
    res = this;
  }
  delete this;
  return res;
}

void PlaceHolder::setAttributes( const string&s ){
  KWargs args;
  args["text"] = s;
  Word::setAttributes( args );
}

KWargs TextContent::collectAttributes() const {
  KWargs attribs = AbstractElement::collectAttributes();
  if ( _offset >= 0 ){
    attribs["offset"] = toString( _offset );
  }
  if ( _newoffset >= 0 ){
    attribs["newoffset"] = toString( _newoffset );
  }
  if ( _length != _text.length() ){
    attribs["length"] = toString( _length );
  }
  if ( _corrected == INLINE ){
    attribs["corrected"] = "inline";
  }
  else if ( _corrected == CORRECTED && 
	    _parent && _parent->getMinCorrectionLevel() < CORRECTED ){
    attribs["corrected"] = "yes";
  }
  return attribs;
}

xmlNode *TextContent::xml( bool ) const {
  xmlNode *e = AbstractElement::xml( false );
  xmlAddChild( e, xmlNewText( (const xmlChar*)str().c_str()) );
  return e;
}

void Description::setAttributes( const KWargs& kwargs ){
  KWargs::const_iterator it;
  it = kwargs.find( "value" );
  if ( it == kwargs.end() ) {
    throw ValueError("value attribute is required for " + classname() );
  }
  _value = it->second;
}

xmlNode *Description::xml( bool ) const {
  xmlNode *e = AbstractElement::xml( false );
  xmlAddChild( e, xmlNewText( (const xmlChar*)_value.c_str()) );
  return e;
}

AbstractElement* Description::parseXml( const xmlNode *node ){
  KWargs att = getAttributes( node );
  KWargs::const_iterator it = att.find("value" );
  if ( it == att.end() ){
    att["value"] = XmlContent( node );
  }
  setAttributes( att );
  return this;
}

AbstractElement *AbstractSpanAnnotation::append( AbstractElement *child ){
  if ( child->isinstance(Word_t) && acceptable( WordReference_t ) )
    child->increfcount();
  AbstractElement::append( child );
  return child;
}

xmlNode *AbstractSpanAnnotation::xml( bool recursive ) const {
  xmlNode *e = AbstractElement::xml( false );
  // append Word children:
  vector<AbstractElement*>::const_iterator it=data.begin();
  while ( it != data.end() ){
    if ( (*it)->element_id() == Word_t ){
      xmlNode *t = newXMLNode( foliaNs(), "wref" );
      KWargs attribs;
      attribs["id"] = (*it)->id();
      string txt = (*it)->str();
      if ( !txt.empty() )
	attribs["t"] = txt;
      addAttributes( t, attribs );
      xmlAddChild( e, t );
    }
    else
      xmlAddChild( e, (*it)->xml( recursive ) );
    ++it;
  }
  return e;
}

xmlNode *Content::xml( bool ) const {
  xmlNode *e = AbstractElement::xml( false );
  xmlAddChild( e, xmlNewCDataBlock( 0,
				    (const xmlChar*)value.c_str() ,
				    value.length() ) );
  return e;
}

AbstractElement* Content::parseXml( const xmlNode *node ){
  KWargs att = getAttributes( node );
  setAttributes( att );
  xmlNode *p = node->children;
  while ( p ){
    if ( p->type == XML_CDATA_SECTION_NODE ){
      value += (char*)p->content;
    }
    p = p->next;
  }
  if ( value.empty() )
    throw XmlError( "CDATA expected in Content node" );
  return this;
}

UnicodeString Correction::text( TextCorrectionLevel corr ) const {
  UnicodeString result;
  for( size_t i=0; i < data.size(); ++i ){
    //    cerr << "data[" << i << "]=" << data[i] << endl;
    if ( data[i]->isinstance( New_t ) || data[i]->isinstance( Current_t ) )
      result += data[i]->text( corr );
  }
  return result;
}

bool Correction::hasNew( ) const {
  vector<AbstractElement*> v = select( New_t, false );
  return !v.empty();
}

AbstractElement *Correction::getNew( int index ) const {
  vector<AbstractElement*> v = select( New_t, false );
  if ( v.empty() )
    throw NoSuchAnnotation("new");
  if ( index < 0 )
    return v[0];
  else
    return v[0]->index(index);
}

bool Correction::hasOriginal() const { 
  vector<AbstractElement*> v = select( Original_t, false );
  return !v.empty();
}

AbstractElement *Correction::getOriginal( int index ) const { 
  vector<AbstractElement*> v = select( Original_t, false );
  if ( v.empty() )
    throw NoSuchAnnotation("original");
  if ( index < 0 )
    return v[0];
  else 
    return v[0]->index(index);
}

bool Correction::hasCurrent( ) const { 
  vector<AbstractElement*> v = select( Current_t, false );
  return !v.empty();
}

AbstractElement *Correction::getCurrent( int index ) const { 
  vector<AbstractElement*> v = select( Current_t, false );
  if ( v.empty() )
    throw NoSuchAnnotation("current");
  if ( index < 0 )
    return v[0];
  else 
    return v[0]->index(index);
}

bool Correction::hasSuggestions( ) const { 
  vector<AbstractElement*> v = suggestions();
  return !v.empty();
}

vector<AbstractElement*> Correction::suggestions( ) const {
  return select( Suggestion_t, false );
}

AbstractElement *Correction::getSuggestion( int index ) const { 
  vector<AbstractElement*> v = suggestions();
  if ( v.empty() )
    throw NoSuchAnnotation("suggestion");
  if ( index < 0 )
    return v[0];
  else
    return v[0]->index(index);
}

AbstractElement *Division::head() const {
  if ( data.size() > 0 ||
       data[0]->element_id() == Head_t ){
    return data[0];
  }
  else
    throw runtime_error( "No head" );
  return 0;
}
  
string Gap::content() const {
  vector<AbstractElement*> cv = select( Content_t );  
  if ( cv.size() < 1 )
    throw NoSuchAnnotation( "content" );
  else {
    return cv[0]->content();
  }
}

void FoLiA::init(){
  _xmltag="FoLiA";
  _element_id = BASE;
  const ElementType accept[] = { Text_t };
  _accepted_data = std::set<ElementType>(accept, accept+1); 
}

void DCOI::init(){
  _xmltag="DCOI";
  _element_id = BASE;
  const ElementType accept[] = { Text_t };
  _accepted_data = std::set<ElementType>(accept, accept+1); 
}

void TextContent::init(){
  _element_id = TextContent_t;
  _xmltag="t";
  _corrected = CORRECTED;
  _offset = -1;
  _newoffset = -1;
  _length = 0;
}

void Head::init() {
  _element_id = Head_t;
  _xmltag="head";
  const ElementType accept[] = { Sentence_t };
  _accepted_data = std::set<ElementType>(accept, accept+1); 
  _annotation_type = AnnotationType::TOKEN;
  TEXTDELIMITER = " ";
}

void Division::init(){
  _xmltag="div";
  _element_id = Division_t;
  const ElementType accept[] = { Head_t, Paragraph_t };
  _accepted_data = std::set<ElementType>(accept, accept+2); 
  _annotation_type = AnnotationType::TOKEN;
}

void Word::init(){
  _xmltag="w";
  _element_id = Word_t;
  const ElementType accept[] = { Text_t, TextContent_t, Pos_t, Lemma_t, Alternative_t, 
				 Correction_t, ErrorDetection_t, Description_t,
				 Morphology_t };
  _accepted_data = std::set<ElementType>(accept, accept+9);
  _annotation_type = AnnotationType::TOKEN;
  _required_attributes = ID;
  _optional_attributes = CLASS|ANNOTATOR|CONFIDENCE;
  MINTEXTCORRECTIONLEVEL = CORRECTED;
  TEXTDELIMITER = " ";
  space = true;
}

void WordReference::init(){
  _required_attributes = ID;
  _xmltag = "wref";
  _element_id = WordReference_t;
  //      ANNOTATIONTYPE = AnnotationType.TOKEN
}

void PlaceHolder::init(){
  _xmltag="placeholder";
  _element_id = PlaceHolder_t;
  const ElementType accept[] = { TextContent_t };
  _accepted_data = std::set<ElementType>(accept, accept+1);
  _annotation_type = AnnotationType::TOKEN;
  _required_attributes = NO_ATT;
  MINTEXTCORRECTIONLEVEL = CORRECTED;
  TEXTDELIMITER = " ";
}


void Gap::init(){
  _xmltag = "gap";
  _element_id = Gap_t;
  const ElementType accept[] = { Content_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2); 
  _optional_attributes = ID|CLASS|ANNOTATOR|CONFIDENCE|N;
}

void Content::init(){
  _xmltag = "content";
  _element_id = Content_t;
}

void Sentence::init(){
  _xmltag="s";
  _element_id = Sentence_t;
  const ElementType accept[] = { Word_t, TextContent_t, Annolay_t, 
				 SyntaxLayer_t,
				 Quote_t,
				 Correction_t,
				 Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+5); 
  _required_attributes = ID;
  _optional_attributes = N;
}

void Text::init(){
  _xmltag="text";
  _element_id = Text_t;
  const ElementType accept[] = { Division_t, Paragraph_t, Sentence_t, Gap_t };
  _accepted_data = std::set<ElementType>(accept, accept+4); 
  _required_attributes = ID;
  TEXTDELIMITER = "\n\n";
}

void Paragraph::init(){
  _xmltag="p";
  _element_id = Paragraph_t;
  const ElementType accept[] = { Sentence_t, Head_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
  _required_attributes = ID;
}

void SyntacticUnit::init(){
  _required_attributes = NO_ATT;
  _optional_attributes = ID|CLASS|ANNOTATOR|CONFIDENCE|DATETIME;
  _xmltag = "su";
  _element_id = SyntacticUnit_t;
  _annotation_type = AnnotationType::SYNTAX;
  const ElementType accept[] = { SyntacticUnit_t, Word_t, WordReference_t,
				 Feature_t };
  _accepted_data = std::set<ElementType>(accept, accept+4);
}

void Chunk::init(){
  _required_attributes = NO_ATT;
  _optional_attributes = ID|CLASS|ANNOTATOR|CONFIDENCE|DATETIME;
  _xmltag = "chunk";
  _element_id = Chunk_t;
  _annotation_type = AnnotationType::CHUNKING;
  const ElementType accept[] = { Word_t, WordReference_t, 
				 Description_t, Feature_t };
  _accepted_data = std::set<ElementType>(accept, accept+4);
}

void Entity::init(){
  _required_attributes = NO_ATT;
  _optional_attributes = ID|CLASS|ANNOTATOR|CONFIDENCE|DATETIME;
  _xmltag = "entity";
  _element_id = Entity_t;
  _annotation_type = AnnotationType::ENTITY;
  const ElementType accept[] = { Word_t, WordReference_t, 
				 Description_t, Feature_t };
  _accepted_data = std::set<ElementType>(accept, accept+4);
}

void AbstractAnnotationLayer::init(){
  _optional_attributes = CLASS;
  _element_id = Annolay_t;
  PRINTABLE=false;
}

void Alternative::init(){
  _required_attributes = ID;
  _xmltag = "alt";
  _element_id = Alternative_t;
    const ElementType accept[] = { Pos_t, Lemma_t, Correction_t };
  _accepted_data = std::set<ElementType>(accept, accept+3);
  _annotation_type = AnnotationType::ALTERNATIVE;
  PRINTABLE = false;
}

void NewElement::init(){
  _xmltag = "new";
  _element_id = New_t;
  const ElementType accept[] = { Pos_t, Lemma_t, Word_t, TextContent_t };
  _accepted_data = std::set<ElementType>(accept, accept+4);
  MINTEXTCORRECTIONLEVEL = CORRECTED;
}

void Current::init(){
  _xmltag = "current";
  _element_id = Current_t;
  const ElementType accept[] = { Pos_t, Lemma_t, Word_t, TextContent_t };
  _accepted_data = std::set<ElementType>(accept, accept+4);
}

void Original::init(){
  _xmltag = "original";
  _element_id = Original_t;
  const ElementType accept[] = { Pos_t, Lemma_t, TextContent_t, Word_t };
  _accepted_data = std::set<ElementType>(accept, accept+4);
  MINTEXTCORRECTIONLEVEL = CORRECTED;
}

void Suggestion::init(){
  _xmltag = "suggestion";
  _element_id = Suggestion_t;
  const ElementType accept[] = { Pos_t, Lemma_t, TextContent_t, Word_t };
  _accepted_data = std::set<ElementType>(accept, accept+4);
  _optional_attributes = ANNOTATOR|CONFIDENCE|DATETIME;
  MINTEXTCORRECTIONLEVEL = CORRECTED;
}

void Correction::init(){
  _xmltag = "correction";
  _element_id = Correction_t;
  _required_attributes = ID;
  _optional_attributes = CLASS|ANNOTATOR|CONFIDENCE|DATETIME;
  _annotation_type = AnnotationType::CORRECTION;
  const ElementType accept[] = { New_t, Original_t, Suggestion_t, Current_t,
				 Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+5);
}

void Description::init(){
  _xmltag = "desc";
  _element_id = Description_t;
}

void ErrorDetection::setAttributes( const KWargs& kwargs ){
  KWargs::const_iterator it = kwargs.find( "error" );
  if ( it != kwargs.end() ) {
    string tmp = lowercase( it->second );
    if ( tmp == "no" || tmp == "false" )
      error = false;
    else
      error = true;
  }
  AbstractElement::setAttributes(kwargs);
}

KWargs ErrorDetection::collectAttributes() const {
  KWargs attribs = AbstractElement::collectAttributes();
  if ( error )
    attribs["error"] = "yes";
  return attribs;
}

void Feature::setAttributes( const KWargs& kwargs ){
  //
  // Feature is special. So DON'T call ::setAttributes
  //
  KWargs::const_iterator it = kwargs.find( "subset" );
  if ( it == kwargs.end() ){
    if ( _subset.empty() ) {
      throw ValueError("subset attribute is required for " + classname() );
    }
  }
  else {
    _subset = it->second;
  }
  it = kwargs.find( "cls" );
  if ( it == kwargs.end() )
    it = kwargs.find( "class" );
  if ( it == kwargs.end() ) {
    throw ValueError("class attribute is required for " + classname() );
  }
  _cls = it->second;
}

KWargs Feature::collectAttributes() const {
  KWargs attribs = AbstractElement::collectAttributes();
  attribs["subset"] = _subset;
  return attribs;
}

std::string AbstractAnnotation::feat( const std::string& s ) const {
  vector<AbstractElement*> v = select( Feature_t, false );
  vector<AbstractElement*>::const_iterator it = v.begin();
  while ( it != v.end() ){
    if ( (*it)->subset() == s )
      return (*it)->cls();
    ++it;
  }
  return "";
}

void AbstractSubtokenAnnotationLayer::init(){

}

void Morpheme::init(){
  _element_id = Morpheme_t;
  _xmltag = "morpheme";
  _required_attributes = NO_ATT;
  _optional_attributes = ID|CLASS|ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, TextContent_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
  _annotation_type = AnnotationType::MORPHOLOGICAL;
  MINTEXTCORRECTIONLEVEL = CORRECTED;
}

void Subentity::init(){
  _element_id = Subentity_t;
  _xmltag = "subentity";
  _required_attributes = CLASS;
  _optional_attributes = ID|ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, TextContent_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
  _annotation_type = AnnotationType::SUBENTITY;
}

void SyntaxLayer::init(){
  //  _element_id = SyntaxLayer_t;
  _xmltag = "syntax";
  const ElementType accept[] = { SyntacticUnit_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void ChunkingLayer::init(){
  //  _element_id = Chunking_t;
  _xmltag = "chunking";
  const ElementType accept[] = { Chunk_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void EntitiesLayer::init(){
  //  _element_id = Entities_t;
  _xmltag = "chunking";
  const ElementType accept[] = { Entity_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void MorphologyLayer::init(){
  _element_id = Morphology_t;
  _xmltag = "morphology";
  const ElementType accept[] = { Morpheme_t };
  _accepted_data = std::set<ElementType>(accept, accept+1);
}

void SubentitiesLayer::init(){
  _element_id = Subentities_t;
  _xmltag = "subentities";
  const ElementType accept[] = { Subentity_t };
  _accepted_data = std::set<ElementType>(accept, accept+1);
}

void PosAnnotation::init(){
  _xmltag="pos";
  _element_id = Pos_t;
  _annotation_type = AnnotationType::POS;
  _required_attributes = CLASS;
  _optional_attributes = ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void LemmaAnnotation::init(){
  _xmltag="lemma";
  _element_id = Lemma_t;
  _annotation_type = AnnotationType::LEMMA;
  _required_attributes = CLASS;
  _optional_attributes = ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void PhonAnnotation::init(){
  _xmltag="phon";
  _element_id = Phon_t;
  _annotation_type = AnnotationType::PHON;
  _required_attributes = CLASS;
  _optional_attributes = ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void DomainAnnotation::init(){
  _xmltag="domain";
  _element_id = Domain_t;
  _annotation_type = AnnotationType::DOMEIN;
  _required_attributes = CLASS;
  _optional_attributes = ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void SenseAnnotation::init(){
  _xmltag="sense";
  _element_id = Sense_t;
  _annotation_type = AnnotationType::SENSE;
  _required_attributes = CLASS;
  _optional_attributes = ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, SynsetFeature_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+3);
}

void SubjectivityAnnotation::init(){
  _xmltag="subjectivity";
  _element_id = Subjectivity_t;
  _annotation_type = AnnotationType::SUBJECTIVITY;
  _required_attributes = CLASS;
  _optional_attributes = ANNOTATOR|CONFIDENCE|DATETIME;
  const ElementType accept[] = { Feature_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+2);
}

void Quote::init(){
  _xmltag="quote";
  _element_id = Quote_t;
  _required_attributes = NO_ATT;
  _optional_attributes = ID;
  const ElementType accept[] = { Word_t, Sentence_t, Quote_t, 
				 TextContent_t, Description_t };
  _accepted_data = std::set<ElementType>(accept, accept+5);
}


void SynsetFeature::init(){
  _xmltag="synset";
  _element_id = Domain_t;
  _annotation_type = AnnotationType::SENSE;
  _subset = "synset";
}

void AbstractSubtokenAnnotation::init() {
  occurrences_per_set = 0; // Allow duplicates within the same set
}

void Feature::init() {
  _xmltag = "feat";
  _element_id = Feature_t;
  _required_attributes = CLASS;
}

void ErrorDetection::init(){
  _xmltag = "errordetection";
  _element_id = ErrorDetection_t;
  _optional_attributes = CLASS|ANNOTATOR|CONFIDENCE|DATETIME;
  _annotation_type = AnnotationType::ERRORDETECTION;
  error = true;
}

