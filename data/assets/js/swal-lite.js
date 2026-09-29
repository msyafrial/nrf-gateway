/**
 * SwalLite - Micro SweetAlert2 Drop-in Replacement for ESP32
 * Zero dependencies, pure vanilla JS (~2.5 KB)
 */
(function(window) {
  let activeModal = null;
  let timerId = null;

  const styleId = "swal-lite-styles";
  if (!document.getElementById(styleId)) {
    const style = document.createElement("style");
    style.id = styleId;
    style.textContent = `
      .swal-lite-overlay{position:fixed;inset:0;background:rgba(2,6,23,0.75);backdrop-filter:blur(8px);display:flex;align-items:center;justify-content:center;z-index:99999;padding:16px;opacity:0;transition:opacity .2s cubic-bezier(0.16,1,0.3,1)}
      .swal-lite-overlay.show{opacity:1}
      .swal-lite-box{background:linear-gradient(135deg,rgba(15,23,42,0.96) 0%,rgba(30,41,59,0.92) 100%);border:1px solid rgba(255,255,255,0.1);border-radius:16px;padding:24px 20px;max-width:400px;width:100%;text-align:center;box-shadow:0 20px 45px rgba(0,0,0,0.6),inset 0 1px 0 rgba(255,255,255,0.1);transform:scale(.92);transition:transform .2s cubic-bezier(0.16,1,0.3,1);font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
      .swal-lite-overlay.show .swal-lite-box{transform:scale(1)}
      .swal-lite-icon{width:56px;height:56px;margin:0 auto 16px;display:flex;align-items:center;justify-content:center;border-radius:50%}
      .swal-lite-icon svg{width:32px;height:32px}
      .swal-lite-icon.success{background:rgba(16,185,129,0.15);color:#10b981;border:1px solid rgba(16,185,129,0.3)}
      .swal-lite-icon.error{background:rgba(239,68,68,0.15);color:#ef4444;border:1px solid rgba(239,68,68,0.3)}
      .swal-lite-icon.warning{background:rgba(245,158,11,0.15);color:#f59e0b;border:1px solid rgba(245,158,11,0.3)}
      .swal-lite-icon.info{background:rgba(56,189,248,0.15);color:#38bdf8;border:1px solid rgba(56,189,248,0.3)}
      .swal-lite-title{font-size:1.15rem;font-weight:700;color:#f8fafc;margin-bottom:10px;line-height:1.3}
      .swal-lite-text{font-size:.9rem;color:#94a3b8;line-height:1.55;margin-bottom:22px}
      .swal-lite-actions{display:flex;gap:12px;justify-content:center}
      .swal-lite-btn{flex:1;min-height:42px;border:none;border-radius:10px;font-size:.9rem;font-weight:600;cursor:pointer;transition:all .15s ease;padding:9px 16px;font-family:inherit;display:inline-flex;align-items:center;justify-content:center}
      .swal-lite-btn:active{transform:scale(.97)}
      .swal-lite-confirm{background:#2ca8ff;color:#fff}
      .swal-lite-confirm:hover{background:#1b94e8}
      .swal-lite-cancel{background:rgba(51,65,85,0.7);color:#cbd5e1;border:1px solid rgba(255,255,255,0.08)}
      .swal-lite-cancel:hover{background:rgba(71,85,105,0.9);color:#f8fafc}
      .swal-lite-spinner{width:36px;height:36px;border:3px solid rgba(255,255,255,0.1);border-top-color:#38bdf8;border-radius:50%;animation:swal-spin .7s linear infinite;margin:8px auto 16px}
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
      this.close();

      return new Promise((resolve) => {
        const overlay = document.createElement("div");
        overlay.className = "swal-lite-overlay";

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
            const cancelStyle = options.cancelButtonColor ? `style="background:${options.cancelButtonColor};color:#fff"` : "";
            actionsHtml += `<button type="button" class="swal-lite-btn swal-lite-cancel" ${cancelStyle}>${cancelText}</button>`;
          }
          if (showConfirm) {
            const confirmStyle = options.confirmButtonColor ? `style="background:${options.confirmButtonColor};color:#fff"` : "";
            actionsHtml += `<button type="button" class="swal-lite-btn swal-lite-confirm" ${confirmStyle}>${confirmText}</button>`;
          }
          actionsHtml += '</div>';
        }

        overlay.innerHTML = `
          <div class="swal-lite-box">
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

        function finish(confirmed) {
          if (timerId) clearTimeout(timerId);
          overlay.classList.remove("show");
          setTimeout(() => {
            if (overlay.parentNode) overlay.parentNode.removeChild(overlay);
            activeModal = null;
          }, 200);
          resolve({ isConfirmed: confirmed, isDismissed: !confirmed });
        }

        const btnConfirm = overlay.querySelector(".swal-lite-confirm");
        if (btnConfirm) btnConfirm.onclick = () => finish(true);

        const btnCancel = overlay.querySelector(".swal-lite-cancel");
        if (btnCancel) btnCancel.onclick = () => finish(false);

        if (options.allowOutsideClick !== false) {
          overlay.onclick = (e) => {
            if (e.target === overlay) finish(false);
          };
        }

        if (options.timer && options.timer > 0) {
          timerId = setTimeout(() => finish(false), options.timer);
        }

        if (typeof options.didOpen === "function") {
          options.didOpen();
        }
      });
    },

    showLoading: function() {
      if (!activeModal) return;
      const slot = activeModal.querySelector("#swal-loading-slot");
      if (slot) slot.innerHTML = '<div class="swal-lite-spinner"></div>';
      const actions = activeModal.querySelector(".swal-lite-actions");
      if (actions) actions.style.display = "none";
    },

    close: function() {
      if (timerId) clearTimeout(timerId);
      if (activeModal) {
        activeModal.classList.remove("show");
        const modal = activeModal;
        setTimeout(() => {
          if (modal.parentNode) modal.parentNode.removeChild(modal);
        }, 150);
        activeModal = null;
      }
    }
  };

  window.Swal = Swal;
})(window);
