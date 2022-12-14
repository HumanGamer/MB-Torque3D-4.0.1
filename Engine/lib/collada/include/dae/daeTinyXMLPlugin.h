/*
 * Copyright 2007 Sony Computer Entertainment Inc.
 *
 * Licensed under the SCEA Shared Source License, Version 1.0 (the "License"); you may not use this 
 * file except in compliance with the License. You may obtain a copy of the License at:
 * http://research.scea.com/scea_shared_source_license.html
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License 
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or 
 * implied. See the License for the specific language governing permissions and limitations under the 
 * License. 
 */

#ifndef __DAE_TINYXMLPLUGIN__
#define __DAE_TINYXMLPLUGIN__

#include <vector>
#include <list>
#include <dae/daeElement.h>
#include <dae/daeURI.h>
#include <dae/daeIOPluginCommon.h>

#ifndef TINYXML2_INCLUDED
#include <tinyxml2.h>
#endif

class daeTinyXMLPlugin : public daeIOPluginCommon
{
public:
	// Constructor / destructor
	/**
	 * Constructor.
	 */
	DLLSPEC daeTinyXMLPlugin();
	/**
	 * Destructor.
	 */
	virtual DLLSPEC ~daeTinyXMLPlugin();

	// Operations
	virtual DLLSPEC daeInt write(const daeURI& name, daeDocument *document, daeBool replace);

	/**
	 * setOption allows you to set options for this IOPlugin. Which options a plugin supports is
	 * dependent on the plugin itself. There is currently no list of options that plugins are
	 * suggested to implement. daeLibXML2Plugin supports only one option, "saveRawBinary". Set to 
	 * "true" to save float_array data as a .raw binary file. The daeRawResolver will convert the 
	 * data back into COLLADA domFloat_array elements upon load.
	 * @param option The option to set.
	 * @param value The value to set the option.
	 * @return Returns DAE_OK upon success.
	 */
	virtual DLLSPEC daeInt setOption( daeString option, daeString value );

	/**
	 * getOption retrieves the value of an option from this IOPlugin. Which options a plugin supports is
	 * dependent on the plugin itself.
	 * @param option The option to get.
	 * @return Returns the string value of the option or NULL if option is not valid.
	 */
	virtual DLLSPEC daeString getOption( daeString option );
	
private:
   tinyxml2::XMLDocument*  m_doc;
	std::list<tinyxml2::XMLElement*>  m_elements;

	virtual daeElementRef readFromFile(const daeURI& uri);
	virtual daeElementRef readFromMemory(daeString buffer, const daeURI& baseUri);
	daeElementRef readElement(tinyxml2::XMLElement* tinyXmlElement, daeElement* parentElement);

	void writeElement( daeElement* element ); 
	void writeAttribute( daeMetaAttribute* attr, daeElement* element );
	void writeValue( daeElement* element );
};

#endif //__DAE_TINYXMLPLUGIN__
