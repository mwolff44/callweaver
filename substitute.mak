substitute = $(SED) \
	-e 's,@prefix\@,$(prefix),g' \
	-e 's,@exec_prefix\@,$(exec_prefix),g' \
	-e 's,@bindir\@,$(bindir),g' \
	-e 's,@sbindir\@,$(sbindir),g' \
	-e 's,@libdir\@,$(libdir),g' \
	-e 's,@sysconfdir\@,$(sysconfdir),g' \
	-e 's,@localstatedir\@,$(localstatedir),g' \
	-e 's,@datadir\@,$(datadir),g' \
	-e 's,@opbxconfdir\@,$(opbxconfdir),g' \
	-e 's,@opbxconffile\@,$(opbxconffile),g' \
	-e 's,@opbxlibdir\@,$(opbxlibdir),g' \
	-e 's,@opbxmoddir\@,$(opbxmoddir),g' \
	-e 's,@opbxvardir\@,$(opbxvardir),g' \
	-e 's,@opbxvardir\@,$(opbxvardir),g' \
	-e 's,@opbxdbdir\@,$(opbxdbdir),g' \
	-e 's,@opbxdbfile\@,$(opbxdbfile),g' \
	-e 's,@opbxtmpdir\@,$(opbxtmpdir),g' \
	-e 's,@opbxrundir\@,$(opbxrundir),g' \
	-e 's,@opbxpidfile\@,$(opbxpidfile),g' \
	-e 's,@opbxsocketfile\@,$(opbxsocketfile),g' \
	-e 's,@opbxlogdir\@,$(opbxlogdir),g' \
	-e 's,@opbxspooldir\@,$(opbxspooldir),g' \
	-e 's,@opbxdatadir\@,$(opbxdatadir),g' \
	-e 's,@opbxdocdir\@,$(opbxdocdir),g' \
	-e 's,@opbxkeydir\@,$(opbxkeydir),g' \
	-e 's,@opbxsqlitedir\@,$(opbxsqlitedir),g' \
	-e 's,@opbxagidir\@,$(opbxagidir),g' \
	-e 's,@opbxsoundsdir\@,$(opbxsoundsdir),g' \
	-e 's,@opbximagesdir\@,$(opbximagesdir),g' \
	-e 's,@opbxmohdir\@,$(opbxmohdir),g' \
	-e 's,@opbxincludedir\@,$(opbxincludedir),g' \
	-e 's,@opbxrunuser\@,$(opbxrunuser),g' \
	-e 's,@opbxrungroup\@,$(opbxrungroup),g'
