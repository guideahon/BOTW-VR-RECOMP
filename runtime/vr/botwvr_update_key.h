// BOTW VR's own update key, apart from Savage Wind's (runtime/platform/sw_update_key.h): a VR AppImage checks an update
// against this one only (runtime/update_appimage.c), so a release signed with Savage Wind's key never passes. There is
// none yet, as BOTW VR's releases aren't signed: Savage Wind Configuration only says a new release is out and where to
// download it. To have it install them, as Savage Wind does:
//   1. make a key, kept outside the repository: openssl ecparam -genkey -name prime256v1 -noout -out botwvr-update.pem
//   2. put its public half here: tools/sign_update.py --public botwvr-update.pem
//   3. sign each AppImage of every release from then on and upload the .sig with it:
//      tools/sign_update.py BOTW-VR-<version>-Linux-x86_64.AppImage botwvr-update.pem
// A release without a matching .sig is then passed over, as in Savage Wind.
#pragma once
// #define BOTWVR_UPDATE_PUBKEY "<128 hex digits, X then Y>"
