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
      .swal-lite-overlay{position:fixed;inset:0;background:rgba(15,23,42,.6);backdrop-filter:blur(3px);display:flex;align-items:center;justify-content:center;z-index:99999;padding:16px;opacity:0;transition:opacity .2s ease}
      .swal-lite-overlay.show{opacity:1}
      .swal-lite-box{background:#fff;border-radius:16px;padding:24px 20px;max-width:380px;width:100%;text-align:center;box-shadow:0 20px 40px rgba(0,0,0,.2);transform:scale(.92);transition:transform .2s ease;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
      .swal-lite-overlay.show .swal-lite-box{transform:scale(1)}
      .swal-lite-icon{width:56px;height:56px;margin:0 auto 16px;display:flex;align-items:center;justify-content:center;border-radius:50%}
      .swal-lite-icon svg{width:32px;height:32px}
      .swal-lite-icon.success{background:#ecfdf5;color:#10b981}
      .swal-lite-icon.error{background:#fef2f2;color:#ef4444}
      .swal-lite-icon.warning{background:#fffbeb;color:#f59e0b}
      .swal-lite-icon.info{background:#f0f9ff;color:#0284c7}
      .swal-lite-title{font-size:1.25rem;font-weight:700;color:#1e293b;margin-bottom:8px;line-height:1.3}
      .swal-lite-text{font-size:.92rem;color:#64748b;line-height:1.5;margin-bottom:20px}
      .swal-lite-actions{display:flex;gap:10px;justify-content:center}
      .swal-lite-btn{flex:1;min-height:42px;border:none;border-radius:10px;font-size:.92rem;font-weight:600;cursor:pointer;transition:all .15s;padding:8px 16px;font-family:inherit}
      .swal-lite-btn:active{transform:scale(.97)}
      .swal-lite-confirm{background:#2ca8ff;color:#fff}
      .swal-lite-confirm:hover{background:#1b94e8}
      .swal-lite-cancel{background:#e2e8f0;color:#475569}
      .swal-lite-cancel:hover{background:#cbd5e1}
      .swal-lite-spinner{width:36px;height:36px;border:3px solid #e2e8f0;border-top-color:#2ca8ff;border-radius:50%;animation:swal-spin .7s linear infinite;margin:8px auto 16px}
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
