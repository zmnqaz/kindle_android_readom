// 「复制这段话」按钮：把 data-copy 指向的元素的文字复制到剪贴板。
// 不支持剪贴板接口时（比如非 https 的本地预览），退回选中文字 + execCommand；都不行就提示手动复制。
(function () {
  'use strict';

  function selectText(el) {
    var range = document.createRange();
    range.selectNodeContents(el);
    var sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
  }

  function legacyCopy(el) {
    selectText(el);
    try { return document.execCommand('copy'); } catch (e) { return false; }
  }

  function report(btn, ok) {
    var status = btn.parentNode.querySelector('.copy-status');
    if (!status) return;
    status.textContent = ok ? btn.getAttribute('data-done') : btn.getAttribute('data-fail');
    status.classList.toggle('ok', ok);
    clearTimeout(btn._t);
    btn._t = setTimeout(function () { status.textContent = ''; }, 4000);
  }

  document.addEventListener('click', function (ev) {
    var btn = ev.target.closest && ev.target.closest('[data-copy]');
    if (!btn) return;
    var el = document.getElementById(btn.getAttribute('data-copy'));
    if (!el) return;
    var text = el.textContent.trim();
    if (navigator.clipboard && window.isSecureContext) {
      navigator.clipboard.writeText(text).then(
        function () { report(btn, true); },
        function () { report(btn, legacyCopy(el)); }
      );
    } else {
      report(btn, legacyCopy(el));
    }
  });
})();
