/*
 * Copyright (C) 2004, 2005, 2006, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006 Rob Buis <buis@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef SVGCircleElement_h
#define SVGCircleElement_h

#include "core/SVGNames.h"
#include "core/svg/SVGAnimatedLength.h"
#include "core/svg/SVGGeometryElement.h"
#include "platform/heap/Handle.h"

namespace blink {

class SVGCircleElement final : public SVGGeometryElement {
  DEFINE_WRAPPERTYPEINFO();

 public:
  DECLARE_NODE_FACTORY(SVGCircleElement);

  Path asPath() const override;

  SVGAnimatedLength* cx() const { return m_cx.get(); }
  SVGAnimatedLength* cy() const { return m_cy.get(); }
  SVGAnimatedLength* r() const { return m_r.get(); }

  DECLARE_VIRTUAL_TRACE();

 private:
  explicit SVGCircleElement(Document&);

  void svgAttributeChanged(const QualifiedName&) override;

  bool isPresentationAttribute(const QualifiedName&) const override;
  bool isPresentationAttributeWithSVGDOM(const QualifiedName&) const override;
  void collectStyleForPresentationAttribute(const QualifiedName&,
                                            const AtomicString&,
                                            MutableStylePropertySet*) override;

  bool selfHasRelativeLengths() const override;

  LayoutObject* createLayoutObject(const ComputedStyle&) override;

  Member<SVGAnimatedLength> m_cx;
  Member<SVGAnimatedLength> m_cy;
  Member<SVGAnimatedLength> m_r;
};

}  // namespace blink

#endif  // SVGCircleElement_h
