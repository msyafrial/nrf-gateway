/**
 * SwalLite - Micro SweetAlert2 Drop-in Replacement
 * Zero dependencies, pure vanilla JS (~2.5 KB)
 */
(function(window) {
  let activeModal = null;
  let timerId = null;
  let activeCleanup = null;
  let activeFinish = null;

  const styleId = "swal-lite-styles";
  if (!document.getElementById(styleId)) {
    const style = document.createElement("style");
    style.id = styleId;
    style.textContent = `
      .swal-lite-overlay{position:fixed;inset:0;background:var(--swal-overlay,rgba(2,6,23,0.75));backdrop-filter:blur(8px);display:flex;align-items:center;justify-content:center;z-index:99999;padding:16px;opacity:0;transition:opacity .2s cubic-bezier(0.16,1,0.3,1)}
      .swal-lite-overlay.show{opacity:1}
      .swal-lite-box{background:var(--swal-bg,linear-gradient(135deg,rgba(15,23,42,0.96) 0%,rgba(30,41,59,0.92) 100%));border:1px solid var(--swal-border,rgba(255,255,255,0.1));border-radius:16px;padding:24px 20px;max-width:400px;width:100%;max-height:calc(100dvh - 24px);overflow-y:auto;overscroll-behavior:contain;text-align:center;box-shadow:var(--swal-shadow,0 20px 45px rgba(0,0,0,0.6),inset 0 1px 0 rgba(255,255,255,0.1));transform:scale(.92);transition:transform .2s cubic-bezier(0.16,1,0.3,1);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
      .swal-lite-box:focus{outline:none}
      .swal-lite-overlay.show .swal-lite-box{transform:scale(1)}
      .swal-lite-icon{width:56px;height:56px;margin:0 auto 16px;display:flex;align-items:center;justify-content:center;border-radius:50%}
      .swal-lite-icon svg{width:32px;height:32px}
      .swal-lite-icon.success{background:var(--swal-icon-success-bg,rgba(16,185,129,0.15));color:var(--swal-icon-success,#10b981);border:1px solid var(--swal-icon-success-bd,rgba(16,185,129,0.3))}
      .swal-lite-icon.error{background:var(--swal-icon-error-bg,rgba(239,68,68,0.15));color:var(--swal-icon-error,#ef4444);border:1px solid var(--swal-icon-error-bd,rgba(239,68,68,0.3))}
      .swal-lite-icon.warning{background:var(--swal-icon-warning-bg,rgba(245,158,11,0.15));color:var(--swal-icon-warning,#f59e0b);border:1px solid var(--swal-icon-warning-bd,rgba(245,158,11,0.3))}
      .swal-lite-icon.info{background:var(--swal-icon-info-bg,rgba(56,189,248,0.15));color:var(--swal-icon-info,var(--accent-cyan,#38bdf8));border:1px solid var(--swal-icon-info-bd,rgba(56,189,248,0.3))}
      .swal-lite-title{font-size:1.15rem;font-weight:700;color:var(--swal-title,#f8fafc);margin-bottom:10px;line-height:1.3}
      .swal-lite-text{font-size:.9rem;color:var(--swal-text,#94a3b8);line-height:1.55;margin-bottom:22px}
      .swal-lite-actions{display:flex;gap:12px;justify-content:center;flex-wrap:wrap}
      .swal-lite-btn{flex:1 1 120px;min-height:42px;border:1px solid transparent;border-radius:10px;font-size:.88rem;font-weight:600;line-height:1.25;cursor:pointer;transition:all .15s ease;padding:8px 14px;font-family:inherit;display:inline-flex;align-items:center;justify-content:center;text-align:center}
      .swal-lite-btn:active{transform:scale(.97)}
      .swal-lite-btn:focus-visible{outline:2px solid var(--focus-ring,#38bdf8);outline-offset:2px}
      .swal-lite-confirm{background:var(--swal-confirm-bg,#38bdf8);color:var(--swal-confirm-fg,#0f172a);border-color:var(--primary-border,rgba(255,255,255,0.22))}
      .swal-lite-confirm:hover{background:var(--swal-confirm-hover,#7dd3fc)}
      .swal-lite-cancel{background:var(--swal-cancel-bg,rgba(51,65,85,0.7));color:var(--swal-cancel-text,#cbd5e1);border:1px solid var(--swal-cancel-border,rgba(255,255,255,0.12))}
      .swal-lite-cancel:hover{background:var(--swal-cancel-hover,rgba(71,85,105,0.9));color:var(--swal-cancel-hover-text,#f8fafc)}
      .swal-lite-spinner{width:36px;height:36px;border:3px solid var(--swal-spinner-track,rgba(255,255,255,0.1));border-top-color:var(--accent-cyan,#38bdf8);border-radius:50%;animation:swal-spin .7s linear infinite;margin:8px auto 16px}
      @keyframes swal-spin{to{transform:rotate(360deg)}}
    `;
    document.head.appendChild(style);
  }

  const icons = {
    success: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"/></svg>',
    error: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="m15 9-6 6M9 9l6 6"/></svg>',
    warning: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><path d="m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3Z"/><path d="M12 9v4M12 17h.01"/></svg>',
    info: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/></svg>'
  };

  const Swal = {
    fire: function(options) {
      if (typeof options === "string") {
        options = { title: arguments[0], text: arguments[1] || "", icon: arguments[2] || "info" };
      }
      options = options || {};
      this.close();

      return new Promise((resolve) => {
        const previousFocus = document.activeElement;
        const overlay = document.createElement("div");
        overlay.className = "swal-lite-overlay";
        overlay.setAttribute("role", "dialog");
        overlay.setAttribute("aria-modal", "true");

        let iconHtml = "";
        if (options.icon && icons[options.icon]) {
          iconHtml = `<div class="swal-lite-icon ${options.icon}">${icons[options.icon]}</div>`;
        }

        const showConfirm = options.showConfirmButton !== false;
        const showCancel = !!options.showCancelButton;
        const confirmText = options.confirmButtonText || "OK";
        const cancelText = options.cancelButtonText || "Cancel";

        let actionsHtml = "";
        if (showConfirm || showCancel) {
          actionsHtml = '<div class="swal-lite-actions">';
          if (showCancel) {
            const rawCancel = (options.cancelButtonColor || "").trim();
            const useDefaultCancel = !rawCancel || rawCancel.toLowerCase() === "#64748b";
            const cancelStyle = useDefaultCancel ? "" : `style="background:${rawCancel};border-color:transparent;color:#fff"`;
            actionsHtml += `<button type="button" class="swal-lite-btn swal-lite-cancel" ${cancelStyle}>${cancelText}</button>`;
          }
          if (showConfirm) {
            const rawConfirm = (options.confirmButtonColor || "").trim();
            const isThemeInfo = !rawConfirm || rawConfirm.includes("--swal-btn-info") || rawConfirm.includes("--primary") || rawConfirm.toLowerCase() === "#38bdf8";
            const confirmStyle = isThemeInfo ? "" : `style="background:${rawConfirm};border-color:transparent;color:#fff"`;
            actionsHtml += `<button type="button" class="swal-lite-btn swal-lite-confirm" ${confirmStyle}>${confirmText}</button>`;
          }
          actionsHtml += '</div>';
        }

        overlay.innerHTML = `
          <div class="swal-lite-box" tabindex="-1">
            ${iconHtml}
            <div id="swal-loading-slot"></div>
            ${options.title ? `<div class="swal-lite-title">${options.title}</div>` : ""}
            ${(options.html || options.text) ? `<div class="swal-lite-text">${options.html || options.text}</div>` : ""}
            ${actionsHtml}
          </div>
        `;

        document.body.appendChild(overlay);
        requestAnimationFrame(() => overlay.classList.add("show"));
        activeModal = overlay;

        const box = overlay.querySelector(".swal-lite-box");
        const actionsEl = overlay.querySelector(".swal-lite-actions");
        const btnConfirm = overlay.querySelector(".swal-lite-confirm");
        const btnCancel = overlay.querySelector(".swal-lite-cancel");
        let closed = false;

        function isButtonVisible(el) {
          return !!(el && (!actionsEl || actionsEl.style.display !== "none"));
        }

        function isLoadingState() {
          return !!(overlay.querySelector(".swal-lite-spinner") && (!actionsEl || actionsEl.style.display === "none"));
        }

        function getFocusableElements() {
          if (!box) return [];
          const nodes = box.querySelectorAll('button:not([disabled]), [href], input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])');
          return Array.prototype.filter.call(nodes, (el) => el.offsetParent !== null);
        }

        function focusInitial() {
          if (closed || activeModal !== overlay) return;
          if (!isLoadingState()) {
            if (options.focusCancel && isButtonVisible(btnCancel)) {
              btnCancel.focus({ preventScroll: true });
              return;
            }
            if (isButtonVisible(btnConfirm)) {
              btnConfirm.focus({ preventScroll: true });
              return;
            }
            if (isButtonVisible(btnCancel)) {
              btnCancel.focus({ preventScroll: true });
              return;
            }
          }
          if (box && typeof box.focus === "function") {
            box.focus({ preventScroll: true });
          }
        }

        function restoreFocus() {
          if (options.restoreFocus === false) return;
          const activeEl = document.activeElement;
          const focusWasInside = !activeEl || activeEl === document.body || overlay.contains(activeEl);
          if (focusWasInside && previousFocus && previousFocus.isConnected && typeof previousFocus.focus === "function") {
            try { previousFocus.focus({ preventScroll: true }); } catch (_) {}
          }
        }

        function onKeyDown(e) {
          if (closed || activeModal !== overlay) return;

          if (e.key === "Tab") {
            const focusables = getFocusableElements();
            if (focusables.length === 0) {
              e.preventDefault();
              if (box && typeof box.focus === "function") box.focus({ preventScroll: true });
              return;
            }
            const first = focusables[0];
            const last = focusables[focusables.length - 1];
            const activeEl = document.activeElement;
            if (!box.contains(activeEl) || activeEl === box) {
              e.preventDefault();
              (e.shiftKey ? last : first).focus({ preventScroll: true });
              return;
            }
            if (e.shiftKey && activeEl === first) {
              e.preventDefault();
              last.focus({ preventScroll: true });
            } else if (!e.shiftKey && activeEl === last) {
              e.preventDefault();
              first.focus({ preventScroll: true });
            }
            return;
          }

          const loading = isLoadingState();

          if (e.key === "Escape" || e.key === "Esc") {
            e.preventDefault();
            e.stopPropagation();
            if (e.repeat || loading) return;
            const allowEsc = options.allowEscapeKey !== false &&
              (options.allowOutsideClick !== false || isButtonVisible(btnCancel) || isButtonVisible(btnConfirm));
            if (allowEsc) finish(false);
            return;
          }

          if (e.key === "Enter") {
            const target = e.target;
            const tag = target && target.tagName ? target.tagName.toUpperCase() : "";
            if (tag === "TEXTAREA" && box && box.contains(target)) {
              e.stopPropagation();
              return;
            }
            e.preventDefault();
            e.stopPropagation();
            if (e.repeat || loading) return;

            const activeEl = document.activeElement;
            if (btnCancel && activeEl === btnCancel && isButtonVisible(btnCancel)) {
              finish(false);
              return;
            }
            if (target && box && box.contains(target) && tag === "A") {
              target.click();
              return;
            }
            if (target && box && box.contains(target) && tag === "BUTTON" && target !== btnConfirm && target !== btnCancel) {
              target.click();
              return;
            }
            if (isButtonVisible(btnConfirm)) {
              finish(true);
            } else if (isButtonVisible(btnCancel)) {
              finish(false);
            }
          }
        }

        function cleanup() {
          document.removeEventListener("keydown", onKeyDown, true);
          if (timerId) {
            clearTimeout(timerId);
            timerId = null;
          }
        }

        function finish(confirmed) {
          if (closed) return;
          closed = true;
          cleanup();
          if (activeModal === overlay) activeModal = null;
          if (activeCleanup === cleanup) activeCleanup = null;
          if (activeFinish === finish) activeFinish = null;
          overlay.classList.remove("show");
          overlay.classList.add("swal-lite-closing");
          overlay.style.pointerEvents = "none";
          setTimeout(() => {
            if (overlay.parentNode) overlay.parentNode.removeChild(overlay);
          }, 200);
          restoreFocus();
          resolve({ isConfirmed: confirmed, isDismissed: !confirmed });
        }

        activeCleanup = cleanup;
        activeFinish = finish;
        document.addEventListener("keydown", onKeyDown, true);

        if (btnConfirm) btnConfirm.onclick = () => finish(true);
        if (btnCancel) btnCancel.onclick = () => finish(false);

        if (options.allowOutsideClick !== false) {
          overlay.onclick = (e) => {
            if (e.target === overlay && !isLoadingState()) finish(false);
          };
        }

        if (options.timer && options.timer > 0) {
          timerId = setTimeout(() => finish(false), options.timer);
        }

        if (typeof options.didOpen === "function") {
          options.didOpen();
        }

        if (!closed && (!box || !box.contains(document.activeElement) || document.activeElement === box)) {
          focusInitial();
        }
      });
    },

    showLoading: function() {
      if (!activeModal) return;
      const slot = activeModal.querySelector("#swal-loading-slot");
      if (slot) slot.innerHTML = '<div class="swal-lite-spinner"></div>';
      const actions = activeModal.querySelector(".swal-lite-actions");
      if (actions) actions.style.display = "none";
      const box = activeModal.querySelector(".swal-lite-box");
      if (box && typeof box.focus === "function") {
        box.focus({ preventScroll: true });
      }
    },

    close: function() {
      if (timerId) {
        clearTimeout(timerId);
        timerId = null;
      }
      if (activeFinish) {
        activeFinish(false);
        return;
      }
      if (activeCleanup) {
        const fn = activeCleanup;
        activeCleanup = null;
        fn();
      }
      if (activeModal) {
        const modal = activeModal;
        activeModal = null;
        modal.classList.remove("show");
        modal.classList.add("swal-lite-closing");
        modal.style.pointerEvents = "none";
        setTimeout(() => {
          if (modal.parentNode) modal.parentNode.removeChild(modal);
        }, 150);
      }
    }
  };

  window.Swal = Swal;
})(window);
