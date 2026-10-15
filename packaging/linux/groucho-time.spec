Name:           groucho-time
Version:        %{version}
Release:        1%{?dist}
Summary:        Groucho Time, a cut-out screensaver clock of Groucho Marx as Father Time
License:        MIT
Source0:        groucho-time-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
Groucho Time, a Monty Python style cut-out screensaver clock: Father Time, from
a nineteenth-century clock design, with Groucho Marx's head pasted on. The
hands go round the engraving's own dial, Groucho's eyes swivel to and fro with
the seconds, his eyebrows waggle every minute, and on the hour the top of his
head flips open with one of his lines. Now and then Gummo peeks in.

On Wayland desktops it starts by itself after five minutes idle.
groucho-time-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/groucho-time

%files
%license LICENSE
%doc README.md
%{_bindir}/groucho-time
%{_bindir}/groucho-time-screensaver
%{_datadir}/applications/groucho-time.desktop
%dir %{_datadir}/screensaver-service
%dir %{_datadir}/screensaver-service/savers
%{_datadir}/screensaver-service/savers/groucho-time.desktop
%{_datadir}/icons/hicolor/256x256/apps/groucho-time.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/groucho-time-screensaver.desktop
