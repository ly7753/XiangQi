export PREFIX="/data/user/0/com.termux/files/usr"
export ACS_FILES="/data/user/0/com.tom.rv2ide/files"
export PATH="$ACS_FILES/usr/bin:$PREFIX/bin:$PATH"
export JAVA_HOME="$ACS_FILES/usr/lib/jvm/java-21-openjdk"
python clean.py
sh ./gradlew clean