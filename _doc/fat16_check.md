## 네이티브 마운트

~~~bash
DEV=$(hdiutil attach -nomount -readonly build/cmake/data.img | awk '{print $1}' | head -1)
MNT=$(mktemp -d); mount -t msdos -r "$DEV" "$MNT"
ls "$MNT"
umount "$MNT"; hdiutil detach "$DEV"; rmdir "$MNT"
~~~

## 형식 검증
~~~bash
file build/cmake/data.img
fsck_msdos -n build/cmake/data.img
~~~
