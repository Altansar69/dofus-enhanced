# Données des ressources

Le catalogue de la r77 contient les **86 ressources de récolte** proposées par
la [carte DofusDB](https://dofusdb.fr/fr/tools/map), dont les **16 minerais**.
Source : API publique DofusDB, `/skills` et `/recoltables2`, consultée le
20 octobre 2026. L’utilisateur a indiqué disposer de l’accord de DofusDB
pour cette intégration.

Les compétences sont regroupées par objet récolté et métier. Les métiers de
récolte sont Bûcheron, Mineur, Alchimiste, Paysan et Pêcheur ; Base contient
l’eau, comme sur DofusDB. Le niveau affiché est le niveau de récolte.
Un objet reste dans le catalogue même si le fournisseur n’a pas d’emplacement.

Les 4 999 cartes recensées fournissent 7 773 couples carte/ressource retenus.
Le monde, les coordonnées et les quantités viennent de chaque relevé, sans
répartition estimée des totaux de sous-zone. Les cartes partageant des
coordonnées sont additionnées, en conservant l’indication des intérieurs.
Les cristaux de Pandala se trouvent dans leur monde de carte correspondant.
Les chiffres indiquent des points de récolte recensés ; ils ne disent pas si
une ressource a déjà été récoltée, ni si ses conditions d’accès sont remplies.

Les contrôles et images sont ceux du client installé. Aucune image Dofus ou
DofusDB n’est distribuée dans ce dépôt ni dans les archives du mod.

`tools/generate-resource-map.py` produit `resource-map-generated.hpp` depuis
deux tableaux JSON regroupant toutes les pages des endpoints. Il vérifie les
identifiants uniques des cartes, les coordonnées, les limites des quantités et
la concordance entre les points individuels et les totaux par objet.
Les réponses brutes et les analyses locales ne sont pas publiées.
