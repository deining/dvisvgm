/***********************************************************************
** DVIToSVGActions.cpp                                                **
**                                                                    **
** This file is part of dvisvgm -- the DVI to SVG converter           **
** Copyright (C) 2005-2009 Martin Gieseking <martin.gieseking@uos.de> **
**                                                                    **
** This program is free software; you can redistribute it and/or      **
** modify it under the terms of the GNU General Public License        **
** as published by the Free Software Foundation; either version 2     **
** of the License, or (at your option) any later version.             **
**                                                                    **
** This program is distributed in the hope that it will be useful,    **
** but WITHOUT ANY WARRANTY; without even the implied warranty of     **
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the      **
** GNU General Public License for more details.                       **
**                                                                    **
** You should have received a copy of the GNU General Public License  **
** along with this program; if not, write to the Free Software        **
** Foundation, Inc., 51 Franklin Street, Fifth Floor,                 **
** Boston, MA 02110-1301, USA.                                        **
***********************************************************************/

#include <cstring>
#include "BoundingBox.h"
#include "CharmapTranslator.h"
#include "DVIReader.h"
#include "DVIToSVG.h"
#include "DVIToSVGActions.h"
#include "Font.h"
#include "FontManager.h"
#include "SpecialManager.h"
#include "XMLNode.h"
#include "XMLString.h"

///////////////////////////////////
// special handlers

#include "BgColorSpecialHandler.h"
#include "ColorSpecialHandler.h"
#include "DvisvgmSpecialHandler.h"
#include "EmSpecialHandler.h"
//#include "HtmlSpecialHandler.h"
#include "TpicSpecialHandler.h"
///////////////////////////////////


using namespace std;

DVIToSVGActions::Nodes::Nodes (XMLElementNode *r) {
	root = r;
	page = font = text = 0;
}


DVIToSVGActions::DVIToSVGActions (const DVIReader &reader, XMLElementNode *svgelem) 
	: _dviReader(reader), _specialManager(0), _color(Color::BLACK), _bgcolor(Color::WHITE),
	_nodes(svgelem), _transMatrix(0) 
{
	_xmoved = _ymoved = false;
	_currentFont = -1;
	_pageCount = 0;
}


DVIToSVGActions::~DVIToSVGActions () {
	delete _transMatrix;
	delete _specialManager;
	FORALL (_charmapTranslatorMap, CharmapTranslatorMap::iterator, i)
		delete i->second;
}


/** Enables or disables processing of specials. If ignorelist == 0, all 
 *  supported special handlers are loaded. To disable selected sets of specials,
 *  the corresponding prefixes can be given separated by non alpha-numeric characters,
 *  e.g. "color, ps, em" or "color: ps em" etc.
 *  A single "*" in the ignore list disables all specials.
 *  @param[in] ignorelist list of special prefixes to ignore 
 *  @return the SpecialManager that handles special statements */
const SpecialManager* DVIToSVGActions::setProcessSpecials (const char *ignorelist) {
	if (ignorelist && strcmp(ignorelist, "*") == 0) { // ignore all specials?
		delete _specialManager;  // then we don't need a SpecialManager
		_specialManager = 0;
	}
	else {
		// add special handlers
		SpecialHandler *handlers[] = {
			new BgColorSpecialHandler,  // handles background color special
			new ColorSpecialHandler,    // handles color specials
			new DvisvgmSpecialHandler,  // handles raw SVG embeddings 
			new EmSpecialHandler,       // handles emTeX specials
//			new HtmlSpecialHandler,     // handles hyperref specials
			new TpicSpecialHandler,     // handles tpic specials
			0
		};
		delete _specialManager;      // delete current SpecialManager
		_specialManager = new SpecialManager;
		_specialManager->registerHandlers(handlers, ignorelist);
	}
	return _specialManager;
}


void DVIToSVGActions::setTransformation (const TransformationMatrix &matrix) {
	delete _transMatrix;
	_transMatrix = new TransformationMatrix(matrix);
}


/** This method is called when a "set char" command was found in the DVI file. 
 *  It draws a character of the current font.
 *  @param[in] x horizontal position of left bounding box edge 
 *  @param[in] y vertical position of the character's baseline 
 *  @param[in] c character code relative to the current font 
 *  @param[in] font font to be used */
void DVIToSVGActions::setChar (double x, double y, unsigned c, const Font *font) {
/*	x *= BP;
	y *= BP; */
	if (DVIToSVG::USE_FONTS) {
		// If we use SVG fonts there is no need to record all font name/char/size combinations
		// because the SVG font mechanism handles this automatically. It's sufficient to
		// record font names and chars. The various font sizes can be ignored here.
		// For a given font object, Font::uniqueFont() returns the same unique font object for
		// all fonts with the same name.
		font = font->uniqueFont();
	}

	const CharmapTranslator *cmt = _charmapTranslatorMap[font];
	_usedCharsMap[font].insert(c);

	if (DVIToSVG::USE_FONTS) {
		XMLTextNode *textNode = new XMLTextNode(XMLString(cmt->unicode(c), false));	

		// create a new tspan element with positioning information
		// if "cursor" was moved
		if (_xmoved || _ymoved || (_color.changed() && _color.get() != Color::BLACK)) {
			_nodes.text = new XMLElementNode("tspan");
			if (_xmoved)
				_nodes.text->addAttribute("x", XMLString(x));
			if (_ymoved)
				_nodes.text->addAttribute("y", XMLString(y));
			if ((_color.changed() ||_xmoved || _ymoved) && _color.get() != Color::BLACK)
				_nodes.text->addAttribute("fill", _color.get().rgbString());
			_nodes.text->append(textNode);
			_nodes.font->append(_nodes.text);
			_xmoved = _ymoved = false;
			_color.changed(false);
		}
		else if (_nodes.text) // no explicit cursor movement => append text to existing node
			_nodes.text->append(textNode);
		else                  // no tspan node and no cursor movement
			_nodes.font->append(textNode);
	}
	else {
		if (_color.changed()) {
			if (_color.get() == Color::BLACK) 
				_nodes.text = 0;
			else {
				_nodes.text = new XMLElementNode("g");
				_nodes.text->addAttribute("fill", _color.get().rgbString());
				_nodes.page->append(_nodes.text);
			}
			_color.changed(false);
		}
		ostringstream oss;
		oss << '#' << _dviReader.getFontManager()->fontID(font) << c;		
		XMLElementNode *use = new XMLElementNode("use");
		use->addAttribute("x", XMLString(x));
		use->addAttribute("y", XMLString(y));
		use->addAttribute("xlink:href", oss.str());
		if (_nodes.text)
			_nodes.text->append(use);
		else
			_nodes.page->append(use);
	}

	// update bounding box
	if (font) {
		/*	x *= BP;
			y *= BP;*/
		double s = font->scaleFactor(); // * BP;
		double w = s*font->charWidth(c);
		double h = s*font->charHeight(c);
		double d = s*font->charDepth(c);
		BoundingBox charbox(x, y-h, x+w, y+d);
		_bbox.embed(charbox);
	}

}


/** This method is called when a "set rule" or "put rule" command was found in the 
 *  DVI file. It draws a solid unrotated rectangle.
 *  @param[in] x horizontal position of left edge 
 *  @param[in] y vertical position of bottom(!) edge 
 *  @param[in] height length of the vertical edges 
 *  @param[in] width length of the horizontal edges */
void DVIToSVGActions::setRule (double x, double y, double height, double width) {
/*	x *= BP;
	y *= BP;
	height *= BP;
	width  *= BP; */
	// (x,y) is the lower left corner of the rectangle
	XMLElementNode *rect = new XMLElementNode("rect");
	rect->addAttribute("x", x);
	rect->addAttribute("y", y-height);
	rect->addAttribute("height", height);
	rect->addAttribute("width", width);
	if (_color.get() != Color::BLACK)
		rect->addAttribute("fill", _color.get().rgbString());
	_nodes.page->append(rect);
	
	// update bounding box
	BoundingBox bb(x, y+height, x+width, y);
	_bbox.embed(bb);
}


void DVIToSVGActions::defineFont (int num, const Font *font) {
	font = font->uniqueFont();
	if (_charmapTranslatorMap.find(font) == _charmapTranslatorMap.end())
		_charmapTranslatorMap[font] = new CharmapTranslator(font);
}


/** This method is called when a "set font" command was found in the DVI file. The
 *  font must be previously defined.
 *  @param[in] num unique number of the font in the DVI file 
 *  @param[in] font pointer to the font object */
void DVIToSVGActions::setFont (int num, const Font *font) {
	if (num != _currentFont && DVIToSVG::USE_FONTS) {
		_nodes.font = new XMLElementNode("text");
		if (DVIToSVG::CREATE_STYLE || !font)
			_nodes.font->addAttribute("class", string("f") + XMLString(num));
		else {
			_nodes.font->addAttribute("font-family", font->name());
			_nodes.font->addAttribute("font-size", font->scaledSize());
		}
//		_nodes.font->addAttribute("x", XMLString(_dviReader.getXPos()*BP));
//		_nodes.font->addAttribute("y", XMLString(_dviReader.getYPos()*BP));
		_nodes.font->addAttribute("x", XMLString(_dviReader.getXPos()));
		_nodes.font->addAttribute("y", XMLString(_dviReader.getYPos()));
		_nodes.page->append(_nodes.font);
		_nodes.text = 0;  // force creating a new _nodes.text when adding next char
		_color.changed(true); 
		_xmoved = _ymoved = false;
		_currentFont = num;
	}
}


/** This method is called when a "special" command was found in the DVI file. 
 *  @param[in] s the special expression */
void DVIToSVGActions::special (const string &s) {
	if (_specialManager) {
		try {
			_specialManager->process(s, this);
			// @@ output message in case of unsupported specials?
		}
		catch (const SpecialException &e) {
			Message::estream(true) << "error in special '" << s << "': " << e.getMessage() << endl;
		}
	}
}


/** This method is called when the DVI preamble was read 
 *  @param[in] cmt preamble comment text. */
void DVIToSVGActions::preamble (const string &cmt) {
}


void DVIToSVGActions::postamble () {
}


/** This method is called when a "begin of page (bop)" command was found in the DVI file. 
 *  @param[in] c array with 10 components representing \count0 ... \count9. c[0] contains the
 *               current (printed) page number (may differ from page count) */
void DVIToSVGActions::beginPage (Int32 *c) {
	_pageCount++;
	_nodes.page = new XMLElementNode("g");
	_nodes.page->addAttribute("id", string("page")+XMLString(int(_pageCount)));
	_nodes.root->append(_nodes.page);
	_xmoved = _ymoved = false;
	_bbox = BoundingBox();  // clear bounding box
}


CharmapTranslator* DVIToSVGActions::getCharmapTranslator (const Font *font) const {
	CharmapTranslatorMap::const_iterator it = _charmapTranslatorMap.find(font);
	if (it != _charmapTranslatorMap.end())
		return it->second;
	return 0;
}


/** This method is called when an "end of page (eop)" command was found in the DVI file. */
void DVIToSVGActions::endPage () {
	_specialManager->notifyEndPage();
	if (_transMatrix)
		_nodes.page->addAttribute("transform", _transMatrix->getSVG());
	if (_bgcolor != Color::WHITE) {
		XMLElementNode *r = new XMLElementNode("rect");
		r->addAttribute("x", _bbox.minX());
		r->addAttribute("y", _bbox.minY());
		r->addAttribute("width", _bbox.width());
		r->addAttribute("height", _bbox.height());
		r->addAttribute("fill", _bgcolor.rgbString());
		_nodes.page->prepend(r);
	}
}


void DVIToSVGActions::appendToPage (XMLNode *node) {
	if (node && _nodes.page)
		_nodes.page->append(node);
}


void DVIToSVGActions::setBgColor (const Color &color) {
	_bgcolor = color;
}
