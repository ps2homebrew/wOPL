#!/bin/bash
# Lang Packer for Open-PS2-Loader
# Made by Caio99BR <caiooliveirafarias0@gmail.com>
# Reworked by Doctor Q <Dr-Q@users.noreply.github.com>

# Set variables
DATE=$(date +'%d %B %Y')
CURRENT_DIR=$(pwd)
BUILD_DIR="$(pwd)/tmp/wOPL_LANG"
LANG_LIST="$(pwd)/tmp/wOPL_LANG_LIST"
make oplversion 2>/dev/null
if [ $? == "0" ]
then
	export wOPL_VERSION=$(make oplversion)
else
	echo "Falling back to old OPL Lang Pack"
	VERSION=$(grep "VERSION =" < "${CURRENT_DIR}/Makefile" | head -1 | cut -d " " -f 3)
	SUBVERSION=$(grep "SUBVERSION =" < "${CURRENT_DIR}/Makefile" | head -1 | cut -d " " -f 3)
	PATCHLEVEL=$(grep "PATCHLEVEL =" < "${CURRENT_DIR}/Makefile" | head -1 | cut -d " " -f 3)
	REVISION=$(($(grep "rev" < "${CURRENT_DIR}/DETAILED_CHANGELOG" | head -1 | cut -d " " -f 1 | cut -c 4-) + 1))
	EXTRAVERSION=$(grep "EXTRAVERSION =" < "${CURRENT_DIR}/Makefile" | head -1 | cut -d " " -f 3)
	if [ "${EXTRAVERSION}" != "" ]; then EXTRAVERSION=-${EXTRAVERSION}; fi
	GIT_HASH=$(git -C "${CURRENT_DIR}/" rev-parse --short=7 HEAD 2>/dev/null)
	if [ "${GIT_HASH}" != "" ]; then GIT_HASH=-${GIT_HASH}; fi
	export wOPL_VERSION=${VERSION}.${SUBVERSION}.${PATCHLEVEL}.${REVISION}${EXTRAVERSION}${GIT_HASH}
fi

# Print a list
mkdir -p "${BUILD_DIR}"
cd "${CURRENT_DIR}/lng/"
printf "$(ls lang_*.lng | cut -c 6- | rev | cut -c 5- | rev)" > "${LANG_LIST}"
cd "${CURRENT_DIR}"

# Copy format
while IFS= read -r CURRENT_FILE
do
	mkdir -p "${BUILD_DIR}/${CURRENT_FILE}-${wOPL_VERSION}/"
	cp "${CURRENT_DIR}/lng/lang_${CURRENT_FILE}.lng" "${BUILD_DIR}/${CURRENT_FILE}-${wOPL_VERSION}/lang_${CURRENT_FILE}.lng"
	if [ -e "lng_src/thirdparty/font_${CURRENT_FILE}.ttf" ]
	then
		cp "${CURRENT_DIR}/lng_src/thirdparty/font_${CURRENT_FILE}.ttf" "${BUILD_DIR}/${CURRENT_FILE}-${wOPL_VERSION}/font_${CURRENT_FILE}.ttf"
	elif [ -e "lng_src/thirdparty/font_${CURRENT_FILE}.otf" ]
	then
		cp "${CURRENT_DIR}/lng_src/thirdparty/font_${CURRENT_FILE}.otf" "${BUILD_DIR}/${CURRENT_FILE}-${wOPL_VERSION}/font_${CURRENT_FILE}.otf"
	fi
done < ${LANG_LIST}

(cat << EOF) > ${BUILD_DIR}/README
-----------------------------------------------------------------------------

  Copyright 2009-2010, Ifcaro & jimmikaelkael
  Copyright 2024, KrahJohilto
  Copyright 2025-Present, Wolf3s, Ripto and chasebocamp
  Licenced under Academic Free License version 3.0
  Review Open PS2 Loader README & LICENSE files for further details.

-----------------------------------------------------------------------------

Double Unofficial Open PS2 Loader Official Translations (${DATE})

HOW TO INSTALL:
1. make sure you are running latest OPL
2. transfer both the the LANGUAGE FILE (.lng) and the FONT FILE (.ttf/.otf) into your OPL directory of your memory card
 a. IMPORTANT: Do not rename the files
 b. NOTE: Some languages don't require a FONT file, so it won't be included
3. run OPL
4. go to OPL Settings
5. open the Display settings page
6. select your new language file
7. press Ok at the bottom of the page
8. then save your settings so next time you run OPL, it will load it with your preferred language file
9. done!
EOF

# Lets pack it!
cd ${BUILD_DIR}/
zip -r "${CURRENT_DIR}/WOPNPS2LD-LANGS-${WOPL_VERSION}.zip" ./*
if [ -f "${CURRENT_DIR}/WOPNPS2LD-LANGS-${WOPL_VERSION}.zip" ]
	then echo "OPL Lang Package Complete: WOPNPS2LD-LANGS-${WOPL_VERSION}.zip"
	else echo "OPL Lang Package not found!"
fi

# Cleanup
cd "${CURRENT_DIR}"
rm -rf ${BUILD_DIR}/ ${LANG_LIST}
unset CURRENT_DIR BUILD_DIR LANG_LIST wOPL_VERSION CURRENT_FILE
