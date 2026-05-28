// SPDX-License-Identifier: MIT
import { useEffect } from 'react';

// Wire Enter = confirm and Escape = cancel for a modal/notification while it is
// `active`. Use this for every confirm-style popup so the keyboard shortcuts are
// consistent. Key events originating from a text field are ignored so typing in
// an input/textarea inside the modal isn't hijacked. Either handler may be
// omitted (e.g. an Esc-only dismissable popup).
export function useConfirmKeys(
  active: boolean,
  handlers: { onConfirm?: () => void; onCancel?: () => void },
): void {
  const { onConfirm, onCancel } = handlers;
  useEffect(() => {
    if (!active) return;
    const onKey = (e: KeyboardEvent) => {
      const t = e.target as HTMLElement | null;
      if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) return;
      if (e.key === 'Enter')       { e.preventDefault(); onConfirm?.(); }
      else if (e.key === 'Escape') { e.preventDefault(); onCancel?.(); }
    };
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [active, onConfirm, onCancel]);
}
