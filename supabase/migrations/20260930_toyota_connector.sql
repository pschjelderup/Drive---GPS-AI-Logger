-- Toyota-kopplingen: bilen som egen kalla for resor och fordonsdata.

-- Enheterna far en sort. 'enhet' ar Hikaya-loggern, 'toyota' ar en bil som
-- lamnar sina resor via Toyota Connected Services. connector bar kopplingens
-- icke-hemliga uppgifter och bilens senaste lage (matarstallning, tank,
-- position, senaste synk och eventuellt fel) - det webbappen visar.
alter table public.drive_devices
  add column if not exists kind text not null default 'enhet',
  add column if not exists connector jsonb;

-- Resor fran en extern kalla bar kallans egen identitet, sa att samma resa
-- aldrig bokfors tva ganger.
alter table public.drive_trips add column if not exists ext_id text;
create unique index if not exists drive_trips_device_ext_id
  on public.drive_trips (device_id, ext_id) where ext_id is not null;

-- Toyota-kontots token. Losenordet sparas aldrig - bara token, och bara
-- har. Tabellen har RLS utan en enda policy och inga rattigheter for
-- anon/authenticated: webblasaren kan varken lasa eller skriva den. Bara
-- molnfunktionen drive-toyota (service-rollen) nar raderna.
create table if not exists public.drive_connector_accounts (
  id bigint generated always as identity primary key,
  provider text not null default 'toyota',
  brand text not null default 'T',
  username text not null,
  uuid text,
  access_token text,
  access_expires timestamptz,
  refresh_token text,
  needs_login boolean not null default false,
  lock_until timestamptz,
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique (provider, username)
);
alter table public.drive_connector_accounts enable row level security;
revoke all on public.drive_connector_accounts from anon, authenticated;
comment on table public.drive_connector_accounts is
  'Token for externa kopplingar (Toyota). Ingen RLS-policy med flit: bara molnfunktionen drive-toyota nar raderna. Losenord sparas aldrig.';
