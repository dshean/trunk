# Put together our App bundle on Mac OS X
if( APPLE )      
   # install imageformat plugins
	install_Qt_ImageFormats( ${MAC_BUNDLE_PLUGIN_DIR} QT_PLUGINS )

   # install support libs
   get_support_libs( SUPPORT_LIBS )
   install( FILES ${SUPPORT_LIBS} DESTINATION ${MAC_BUNDLE_MACOS_DIR} COMPONENT Runtime )
   
   # install qt.conf
   install( CODE "
      file( WRITE ${MAC_BUNDLE_RESOURCES_DIR}/qt.conf \"[Paths]\nPlugins = Plugins\" )
      " COMPONENT Runtime )
   
   set( DIRS ${EXTERNAL_LIBS_LIBRARIES} )
   set( PLUGINS ${QT_PLUGINS} ${CC_PLUGINS} )

   # call fixup_bundles to change paths using otools
   install( CODE "
      include( BundleUtilities )
      fixup_bundle( ${CMAKE_INSTALL_PREFIX}/${qCC_dest_release}/${MAC_APP_NAME} \"${PLUGINS}\" \"${DIRS}\" COMPONENT Runtime ) 
      ")
endif( APPLE )