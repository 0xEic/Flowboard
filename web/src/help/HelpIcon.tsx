// SPDX-License-Identifier: MIT
//
// Small "?" icon that opens the help drawer to a specific node type. Used on
// canvas node headers, in the palette list, in the inspector, and the topbar.
// On canvas the icon must opt out of React Flow's drag-to-move behavior — the
// `nodrag` class plus stopPropagation on mousedown prevents the click from
// turning into a node drag.

import type { MouseEvent } from 'react';
import { openHelp } from './HelpPanel';

type Props = {
  typeName?: string;     // when omitted, the panel opens to the top
  className?: string;    // size/color overrides per call site
  title?: string;
  ariaLabel?: string;
};

export function HelpIcon({ typeName, className, title, ariaLabel }: Props) {
  const onClick = (e: MouseEvent) => {
    e.stopPropagation();
    e.preventDefault();
    openHelp(typeName);
  };
  return (
    <button
      type="button"
      onClick={onClick}
      onMouseDown={e => e.stopPropagation()}
      className={
        'nodrag inline-flex items-center justify-center rounded-full ' +
        'bg-slate-700/70 hover:bg-sky-700 text-slate-100 text-[10px] font-semibold ' +
        'w-4 h-4 leading-none shrink-0 ' +
        (className ?? '')
      }
      title={title ?? (typeName ? `Open help for ${typeName}` : 'Open help')}
      aria-label={ariaLabel ?? title ?? 'Open help'}
    >
      ?
    </button>
  );
}
