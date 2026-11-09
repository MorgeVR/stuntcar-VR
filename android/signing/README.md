# Release signing

`release.jks.enc` is the Quest app's release signing key, encrypted with AES-256
(`openssl enc -aes-256-cbc -pbkdf2 -iter 200000`). The password is stored only as the
GitHub Actions secret `ANDROID_KEYSTORE_PASSWORD` (and in the owner's private backup).
The same password unlocks the keystore; the key alias is `stuntcarvr`.

Every Quest build must be signed with this key, otherwise players can't update over a
previous install. Never commit the decrypted `.jks` or the password.

Without the secret (forks, local builds) the APK is signed with the Android debug key.

Release certificate SHA-256: `9cbdac0411056d703cfe8d498a73e66246a797c4cd35e32a3082937f8bfb1984`
