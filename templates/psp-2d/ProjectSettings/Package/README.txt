Package settings for the EBOOT.PBP (rensga build --target psp, or Build and Run in the editor).

Package.json
  title    shown in the XMB and in PARAM.SFO; default is the project name
  discId   four capital letters and five digits (like RNSG00001); default is made from the project name
  version  <major>.<minor>, like 1.00

Art (all optional): put your own files here and they are used as they are.
  ICON0.png  144x80   the icon in the XMB
  PIC1.png   480x272  the background behind it
  SND0.AT3            the sound played on the icon (the build has no AT3 encoder: bring your own file)
Without them, the build draws placeholders from the project name and the Rensga brand colors.
Every music*.wav of the game and the license files (LICENSES/) are put next to EBOOT.PBP.
