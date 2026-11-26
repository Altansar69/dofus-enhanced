#pragma once
#include "metamob/metamob-sync.hpp"
#include <cstdint>
#include <string>
#include <vector>
// « Échanges Metamob »: the texts of the window (trades-window.cpp) and of the
// chat notices (metamob-sync.cpp), from the proposals the sync read. Pure code:
// no network, no game objects. Every text is plain (never rich text).
namespace metamob {
// A side of a proposal: its first `limit` monsters, « 2 × Nom » from two of a
// kind, « monstre n° 861 » for one the quest list does not name, then how many
// more; « rien » for an empty side.
inline std::string TradeSideText(const std::vector<TradeMonster>& monsters, size_t limit) {
    if (monsters.empty()) return "rien";
    std::string text;
    size_t shown = 0;
    for (const auto& monster : monsters) {
        if (shown == limit) break;
        if (shown++) text += ", ";
        if (monster.quantity > 1) text += std::to_string(monster.quantity) + " × ";
        text += monster.name.empty() ? "monstre n° " + std::to_string(monster.id) : monster.name;
    }
    if (const auto rest = monsters.size() - shown) text += " et " + std::to_string(rest) + (rest > 1 ? " autres" : " autre");
    return text;
}
// How many monsters a side holds, every copy counted.
inline int TradeSideCount(const std::vector<TradeMonster>& monsters) {
    int64_t count = 0;
    for (const auto& monster : monsters) count += monster.quantity;
    return static_cast<int>(count > 1000000 ? 1000000 : count);
}
// How long ago, from two clocks of the same kind (seconds): « à l’instant »
// under a minute or for a date ahead of this PC's clock, then minutes, hours, days.
inline std::string AgeText(int64_t then, int64_t now) {
    const int64_t seconds = now - then;
    if (seconds < 60) return "à l’instant";
    if (seconds < 3600) return "il y a " + std::to_string(seconds / 60) + " min";
    if (seconds < 86400) return "il y a " + std::to_string(seconds / 3600) + " h";
    return "il y a " + std::to_string(seconds / 86400) + " j";
}
// A proposal's first line: who proposes it (the contact character and the
// server when Metamob gives them) and since when.
inline std::string TradeTitle(const TradeProposal& proposal, int64_t nowUnix) {
    std::string text = proposal.sender;
    if (!proposal.character.empty()) text += " (" + proposal.character + ")";
    if (!proposal.server.empty()) text += " · " + proposal.server;
    return text + " · " + AgeText(proposal.createdAt, nowUnix);
}
// Where a proposal stands for the user.
inline std::string TradeStateText(const TradeProposal& proposal) {
    if (proposal.senderApplied) return "Déjà appliquée de son côté : il ne manque que le tien.";
    if (proposal.halfApplied) return "Appliquée de ton côté : en attente du sien.";
    return proposal.read ? "En attente de ta réponse." : "Nouvelle : en attente de ta réponse.";
}
inline std::string TradeReceiveText(const TradeProposal& proposal, size_t limit = 12) {
    return "Tu reçois (" + std::to_string(TradeSideCount(proposal.given)) + ") : " + TradeSideText(proposal.given, limit);
}
inline std::string TradeGiveText(const TradeProposal& proposal, size_t limit = 12) {
    return "Tu donnes (" + std::to_string(TradeSideCount(proposal.received)) + ") : " + TradeSideText(proposal.received, limit);
}
// The window's first line: what the last readings found. nowMs: the sync's
// clock (TradesView::readAt's).
inline std::string TradesSummary(const TradesView& view, uint64_t nowMs) {
    if (!view.usable) return "Aucune clé Metamob utilisable : enregistre ta clé dans les options du mod, section Metamob.";
    std::string trouble;
    switch (view.status) {
    case TradesStatus::Loading: case TradesStatus::Ready: break;
    case TradesStatus::Unreachable: trouble = "Metamob est injoignable, nouvel essai automatique."; break;
    case TradesStatus::Busy: trouble = "Metamob demande d’attendre, nouvel essai automatique."; break;
    case TradesStatus::Refused: trouble = "Metamob refuse cette clé pour les échanges."; break;
    case TradesStatus::KeyUnreadable: trouble = "La clé enregistrée est illisible sur ce compte Windows."; break;
    case TradesStatus::Unavailable: trouble = "Metamob ne donne pas cette liste pour le moment."; break;
    case TradesStatus::Unreadable: trouble = "Réponse de Metamob illisible, nouvel essai automatique."; break;
    }
    if (!view.read) return trouble.empty() ? "Lecture des propositions d’échange…" : trouble;
    const auto count = view.proposals.size();
    std::string text = count == 0 ? "Aucune proposition d’échange en attente." :
        count == 1 ? "1 proposition d’échange reçue." : std::to_string(count) + " propositions d’échange reçues.";
    if (view.more) text += " Seules les 50 plus récentes sont listées.";
    text += " Lu " + AgeText(static_cast<int64_t>(view.readAt / 1000), static_cast<int64_t>(nowMs / 1000)) + ".";
    return trouble.empty() ? text : text + " " + trouble;
}
}
