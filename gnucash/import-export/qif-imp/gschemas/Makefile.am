@INTLTOOL_XML_NOMERGE_RULE@

gschema_in_files = \
  org.gnucash.dialogs.import.qif.gschema.xml.in

gsettings_SCHEMAS = $(gschema_in_files:.gschema.xml.in=.gschema.xml)

@GSETTINGS_RULES@

CLEANFILES = $(gsettings_SCHEMAS)

EXTRA_DIST = CMakeLists.txt
