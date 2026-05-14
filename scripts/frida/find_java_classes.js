'use strict';

// Edit this regex as traces narrow down the target surface.
const PATTERN = /room6|horizon|android\.support|menu|unity|il2cpp/i;

if (!Java.available) {
  console.log('Java runtime unavailable');
} else {
  Java.perform(function () {
    let count = 0;
    Java.enumerateLoadedClasses({
      onMatch(name) {
        if (PATTERN.test(name)) {
          count += 1;
          console.log(name);
        }
      },
      onComplete() {
        console.log('matched classes=' + count);
      }
    });
  });
}
