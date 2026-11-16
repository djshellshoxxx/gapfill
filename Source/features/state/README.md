# State input limits

`XmlLimits.h` checks XML structure before the project-state and parameter-preset
loaders call JUCE's recursive XML parser and `ValueTree::fromXml`. It accepts at
most 64 nested elements and rejects DTD/entity declarations, which are not part
of GapFill's emitted state format. Comments, processing instructions, CDATA and
quoted attribute values do not count as child elements. JUCE remains responsible
for XML syntax; the existing loaders retain semantic validation and the 1 MiB
input limit.

The check uses a non-owning byte view, constant space, linear time, and no JUCE
dependency. A rejected project state does not change the running session; a
rejected preset uses the existing invalid-XML error. Parameter identities,
serialization, migrations and optional-subtree handling are unchanged.

Regression: `tools/test_xml_limits.cpp` exercises depth 1 through 2,000, malformed
lexical constructs, DTDs, and 20,000 shallow siblings. `StateQa` exercises the
actual processor's rejection and verifies the saved state is unchanged.
