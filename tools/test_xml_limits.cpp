#include "../Source/features/state/XmlLimits.h"

#include <cassert>
#include <iostream>
#include <string>

int main()
{
    using gapfill::state::xmlWithinLimits;
    assert (xmlWithinLimits ("<?xml version=\"1.0\"?><GAPFILL_STATE><GAPFILL><PARAM id='wet' value='0.5'/></GAPFILL></GAPFILL_STATE>"));
    assert (xmlWithinLimits ("<r text='&lt;quoted&gt; &amp; >'><![CDATA[<not-a-child/>]]><!-- <ignored> --><a/></r>"));
    assert (! xmlWithinLimits (""));
    assert (! xmlWithinLimits ("<r a='unterminated>"));
    assert (! xmlWithinLimits ("<r><!-- no end</r>"));
    assert (! xmlWithinLimits ("<r><![CDATA[no end</r>"));
    assert (! xmlWithinLimits ("<?xml no end<r/>"));
    assert (! xmlWithinLimits ("<!DOCTYPE r [<!ENTITY x 'payload'>]><r>&x;</r>"));
    assert (! xmlWithinLimits ("<!DOCTYPE r SYSTEM 'file:///external'><r/>"));
    assert (! xmlWithinLimits ("</r>"));
    assert (! xmlWithinLimits ("<r><a/>"));
    assert (! xmlWithinLimits ("<r/>", 0));
    assert (xmlWithinLimits ("<r/>", 1));
    assert (! xmlWithinLimits ("<r><a/></r>", 1));
    assert (xmlWithinLimits ("<r><a/></r>", 2));

    std::string opening, closing;
    for (int depth = 1; depth <= 2000; ++depth)
    {
        opening += "<L>";
        closing += "</L>";
        assert (xmlWithinLimits (opening + closing) == (depth <= 64));
    }
    // Broad, shallow documents must not be mistaken for recursive depth.
    std::string siblings = "<r>";
    for (int i = 0; i < 20000; ++i) siblings += "<PARAM id='p' value='1'/>";
    assert (xmlWithinLimits (siblings + "</r>"));
    std::cout << "XML limits: depth boundary, malformed input, DTDs, lexical exclusions and breadth passed\n";
}
