/* bridge.js — globals the Python side calls via window.evaluate_js (UiBridge).
 * Thin: just forward to the ui* helpers defined in console.js.
 */
window.dbOnConnection = function (connected) { window.uiSetConnection(connected); };
window.dbOnState = function (state, source) { window.uiSetState(state, source); };
window.dbOnMode = function (mode) { window.uiSetMode(mode); };
window.dbOnLog = function (line) { window.uiAppendLog(line); };
window.dbOnUsage = function (u) { window.uiSetUsage && window.uiSetUsage(u); };
